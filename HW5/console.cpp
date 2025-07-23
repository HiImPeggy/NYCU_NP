#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdlib>

using boost::asio::ip::tcp;
using namespace boost::asio;
using namespace std;

#define MAX_CLIENTS 5

struct Connection {
    string host, port, file;
};

string sock4_host;
string sock4_port;

string html_escape(const string& data) {
    string escaped = data;
    boost::replace_all(escaped, "&", "&amp;");
    boost::replace_all(escaped, "\"", "&quot;");
    boost::replace_all(escaped, "'", "&apos;");
    boost::replace_all(escaped, "<", "&lt;");
    boost::replace_all(escaped, ">", "&gt;");
    boost::replace_all(escaped, "\n", "<br>");
    return escaped;
}

void output_shell(int id, const string& content) {
    cout << "<script>document.getElementById('s" << id << "').innerHTML += '"
         << html_escape(content) << "';</script>" << endl;
    cout.flush();
}

void output_command(int id, const string& content) {
    cout << "<script>document.getElementById('s" << id << "').innerHTML += '<b>"
         << html_escape(content) << "</b>';</script>" << endl;
    cout.flush();
}

vector<Connection> parseQueryString(const string& query) {
    vector<Connection> result(MAX_CLIENTS);
    string arg;
    istringstream stream(query);
    int idx = 0;

    while (getline(stream, arg, '&')) {
        if (arg.substr(0, 2) == "sh") {
            sock4_host = arg.substr(3);
            getline(stream, arg, '&');
            sock4_port = arg.substr(3);
            continue;
        }

        size_t pos = arg.find('=');
        if (pos == string::npos) continue;

        string key = arg.substr(0, pos);
        string value = arg.substr(pos + 1);

        if (key[0] == 'h') {
            result[idx].host = value;
        } else if (key[0] == 'p') {
            result[idx].port = value;
        } else if (key[0] == 'f') {
            result[idx].file = value;
            idx++;
        }
    }

    result.resize(idx);
    return result;
}

void show_HTML(const vector<Connection>& connections) {
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html>"
         << "<html lang=\"en\">"
         << "<head>"
         << "<meta charset=\"UTF-8\" />"
         << "<title>NP Project5</title>"
         << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" "
         << "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" crossorigin=\"anonymous\" />"
         << "<link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" rel=\"stylesheet\" />"
         << "<link rel=\"icon\" type=\"image/png\" href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" />"
         << "<style>"
         << "* { font-family: 'Source Code Pro', monospace; font-size: 1rem !important; }"
         << "body { background-color: #212529; }"
         << "pre { color: #cccccc; }"
         << "b { color: #01b468; }"
         << "</style>"
         << "</head>"
         << "<body>"
         << "<table class=\"table table-dark table-bordered\">"
         << "<thead><tr>";

    for (size_t i = 0; i < connections.size(); ++i) {
        if (!connections[i].host.empty()) {
            cout << "<th scope=\"col\">" << connections[i].host << ":" << connections[i].port << "</th>";
        }
    }

    cout << "</tr></thead><tbody><tr>";
    for (size_t i = 0; i < connections.size(); ++i) {
        if (!connections[i].host.empty()) {
            cout << "<td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td>";
        }
    }

    cout << "</tr></tbody></table></body></html>" << endl;
    cout.flush();
}

class ShellSession : public enable_shared_from_this<ShellSession> {
public:
    ShellSession(boost::asio::io_context& io_context, const Connection& conn, int id)
        : resolver_(io_context), socket_(io_context), conn_(conn), id_(id) {}

    void start() {
        if (conn_.host.empty() || conn_.port.empty() || conn_.file.empty()) return;
        file_.open("./test_case/" + conn_.file);

        resolver_.async_resolve(sock4_host, sock4_port,
            [self = shared_from_this()](boost::system::error_code ec, tcp::resolver::results_type results) {
                if (!ec) {
                    self->connect(results);
                }
            }
        );
    }

private:
    tcp::resolver resolver_;
    tcp::socket socket_;
    Connection conn_;
    int id_;
    enum { max_length = 4096 };
    char data_[max_length];
    ifstream file_;
    string command_;

    void connect(const tcp::resolver::results_type& endpoints) {
        auto self = shared_from_this();
        async_connect(socket_, endpoints,
            [self](boost::system::error_code ec, auto) {
                if (!ec) {
                    self->sock4_connect();
                    self->read();
                }
            }
        );
    }

    void sock4_connect() {
        uint8_t version = 4;
        uint8_t code = 1;
        uint16_t destPort = boost::lexical_cast<uint16_t>(conn_.port);
        destPort = (destPort >> 8) | (destPort << 8);
        array<uint8_t, 4> destIP = {0, 0, 0, 1};
        uint8_t null = 0;

        array<boost::asio::const_buffer, 7> request = {
            boost::asio::buffer(&version, 1),
            boost::asio::buffer(&code, 1),
            boost::asio::buffer(&destPort, 2),
            boost::asio::buffer(destIP),
            boost::asio::buffer(&null, 1),
            boost::asio::buffer(conn_.host),
            boost::asio::buffer(&null, 1)
        };
        boost::asio::write(socket_, request);

        array<boost::asio::mutable_buffer, 4> response = {
            boost::asio::buffer(&version, 1),
            boost::asio::buffer(&code, 1),
            boost::asio::buffer(&destPort, 2),
            boost::asio::buffer(destIP)
        };
        boost::asio::read(socket_, response);
    }

    void read() {
        auto self = shared_from_this();
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    string output(data_, length);
                    output_shell(self->id_, output);
                    if (output.find("% ") != string::npos) {
                        self->send_command();
                    } else {
                        self->read();
                    }
                }
            }
        );
    }

    void send_command() {
        if (!getline(file_, command_)) {
            socket_.close();
            return;
        }
        if (!command_.empty() && command_.back() == '\r') {
            command_.pop_back();
        }
        command_ += "\n";
        output_command(id_, command_);

        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(command_),
            [self](boost::system::error_code ec, size_t) {
                if (!ec) {
                    if (self->command_ == "exit\n") {
                        self->socket_.close();
                    } else {
                        self->read();
                    }
                }
            }
        );
    }
};

int main() {
    try {
        io_context io_context;
        string query = getenv("QUERY_STRING") ? getenv("QUERY_STRING") : "";
        vector<Connection> connections = parseQueryString(query);

        show_HTML(connections);

        for (size_t i = 0; i < connections.size(); ++i) {
            if (!connections[i].host.empty()) {
                make_shared<ShellSession>(io_context, connections[i], i)->start();
            }
        }

        io_context.run();
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}