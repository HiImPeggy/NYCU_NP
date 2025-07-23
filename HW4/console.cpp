#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <cstdlib>
#include <sstream>
#include <regex>

using boost::asio::ip::tcp;
using namespace boost::asio;
using namespace std;

#define MAX_CLIENTS 5

struct Connection {
    string host, port, file;
};

string html_escape(const string& data) {
    string escaped = data;
    boost::replace_all(escaped, "&", "&amp;");
    boost::replace_all(escaped, "\"", "&quot;");
    boost::replace_all(escaped, "'", "&apos;");
    boost::replace_all(escaped, "<", "&lt;");
    boost::replace_all(escaped, ">", "&gt;");
    boost::replace_all(escaped, "\r", "");
    boost::replace_all(escaped, "\n", "<br>");
    boost::replace_all(escaped, " ", "&nbsp;");
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
    string idx, value;
    istringstream stream(query);
    while (getline(stream, idx, '=')) {
        getline(stream, value, '&');
        if (idx[0] == 'h') result[(idx[1]-'0')].host = value;
        else if (idx[0] == 'p') result[(idx[1]-'0')].port = value;
        else if (idx[0] == 'f') result[(idx[1]-'0')].file = value;
    }

    return result;
}

void show_HTML(const vector<Connection>& connections) {
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>NP Project</title>";
    cout << R"(
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css">
    <style>
        body { background-color: #212529; color: #fff; }
        pre { color: #7FFF00; }
    </style>
    </head><body><div class='container'>
    <table class='table table-dark table-bordered'><thead><tr>
    )";

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!connections[i].host.empty())
            cout << "<th>" << connections[i].host << ":" << connections[i].port << "</th>";
    }
    cout << "</tr></thead><tbody><tr>";
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!connections[i].host.empty())
            cout << "<td><pre id='s" << i << "' class='mb-0'></pre></td>";
    }
    cout << "</tr></tbody></table></div></body></html>" << endl;
    cout.flush();
}

class ShellSession : public enable_shared_from_this<ShellSession> {
    public:
        ShellSession(boost::asio::io_context& io_context, const Connection& conn, int id)   // arg
            : resolver_(io_context), socket_(io_context), conn_(conn), id_(id) {}   // initialized list

        void start() {
            if (conn_.host.empty() || conn_.port.empty() || conn_.file.empty()) return;
            file_.open("./test_case/" + conn_.file);

            resolver_.async_resolve(conn_.host, conn_.port, // resolve host and port into TCP endpoints
                [self = shared_from_this()](boost::system::error_code ec, tcp::resolver::results_type results) {    // store shared_ptr to self, 
                    if (!ec) self->connect(results);
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
            boost::asio::async_connect(socket_, endpoints,
                [self = shared_from_this()](boost::system::error_code ec, auto) {
                    if (!ec) self->read();
                }
            );
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
            if (!getline(file_, command_)) return;
            command_ += "\n";
            output_command(id_, command_);
            
            boost::asio::async_write(socket_, boost::asio::buffer(command_ + "\0"),
                [self = shared_from_this()](boost::system::error_code ec, auto) {
                    if (!ec) self->read();
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

        for (int i = 0; i < MAX_CLIENTS; ++i) {
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