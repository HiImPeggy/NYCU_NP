#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

using boost::asio::ip::tcp;
using namespace std;

#define MAX_CONNECTIONS 5
#define MAX_NP_SERVERS 12
#define DOMAIN "cs.nycu.edu.tw"
#define BUFFER_SIZE 4096
#define TEST_CASE_DIR "./test_case/"

struct Environment {
    string REQUEST_METHOD;
    string REQUEST_URI;
    string PATH_INFO;
    string QUERY_STRING;
    string SERVER_PROTOCOL;
    string HTTP_HOST;
    string SERVER_ADDR;
    string SERVER_PORT;
    string REMOTE_ADDR;
    string REMOTE_PORT;
};

struct ConnectionInfo {
    int id;
    string host;
    string port;
    string file;
    string full_message;
    tcp::socket* sock;
    ifstream* input_file;
    char data_buffer[BUFFER_SIZE];
};

class HTTPServer {
    public:
        HTTPServer(boost::asio::io_context& io_context, short port)
            : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), io_context_(io_context) {
            boost::asio::socket_base::reuse_address option(true);
            acceptor_.set_option(option);
            do_accept();
        }

    private:
        void do_accept() {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, tcp::socket socket) {
                    if (!ec) {
                        make_shared<WebSession>(move(socket), io_context_)->start();
                    }
                    do_accept();
                });
        }    

        class ClientSession : public enable_shared_from_this<ClientSession> {
            public:
                ClientSession(int id, boost::asio::io_context& io_context, shared_ptr<tcp::socket> web_socket)
                    : id_(id), socket_(io_context), web_socket_(web_socket), resolver_(io_context) {}

                void start() {
                    if (connections[id_].input_file->is_open()) {
                        do_resolve();
                    }
                }

            private:
                void do_resolve() {
                    auto self(shared_from_this());
                    resolver_.async_resolve(
                        connections[id_].host, connections[id_].port,
                        [this, self](boost::system::error_code ec, tcp::resolver::iterator it) {
                            if (!ec) {
                                do_connect(it);
                            } else {
                                cerr << "Resolve error: " << ec.message() << endl;
                            }
                        });
                }

                void do_connect(tcp::resolver::iterator it) {
                    auto self(shared_from_this());
                    connections[id_].sock->async_connect(
                        *it,
                        [this, self](boost::system::error_code ec) {
                            if (!ec) {
                                do_read();
                            } else {
                                cerr << "Connect error: " << ec.message() << endl;
                            }
                        });
                }

                void do_read() {
                    auto self(shared_from_this());
                    connections[id_].sock->async_read_some(
                        boost::asio::buffer(connections[id_].data_buffer, BUFFER_SIZE),
                        [this, self](boost::system::error_code ec, size_t length) {
                            if (!ec) {
                                string content(connections[id_].data_buffer, length);
                                connections[id_].full_message += content;
                                memset(connections[id_].data_buffer, '\0', BUFFER_SIZE);

                                write_shell_output(content);
                                if (content.find("% ") != string::npos) {
                                    write_to_np_server();
                                } else {
                                    do_read();
                                }
                            } else {
                                if (ec == boost::asio::error::eof) {
                                    close_session();
                                } else {
                                    cerr << "Read error: " << ec.message() << endl;
                                }
                            }
                    });
            }

                void write_to_np_server() {
                    auto self(shared_from_this());
                    string command = get_command();
                    boost::asio::async_write(
                        *connections[id_].sock,
                        boost::asio::buffer(command.c_str(), command.length()),
                        [this, self](boost::system::error_code ec, size_t /*length*/) {
                            if (!ec) {
                                do_read();
                            } else {
                                cerr << "Write to NP server error: " << ec.message() << endl;
                            }
                        });
                }

                void write_shell_output(const string& content) {
                    string escaped = html_escape(content);
                    string output = (boost::format("<script>document.getElementById('s%1%').innerHTML += '%2%';</script>")
                                    % id_ % escaped).str();
                    write_to_web(output);
                }

                void write_command_output(const string& command) {
                    string escaped = html_escape(command);
                    string output = (boost::format("<script>document.getElementById('s%1%').innerHTML += '<b>%2%</b>';</script>")
                                    % id_ % escaped).str();
                    write_to_web(output);
                }

                void write_to_web(const string& content) {
                    auto self(shared_from_this());
                    boost::asio::async_write(
                        *web_socket_,
                        boost::asio::buffer(content.c_str(), content.length()),
                        [this, self](boost::system::error_code ec, size_t /*length*/) {
                            if (ec) {
                                cerr << "Write to web error: " << ec.message() << endl;
                            }
                        });
                }

                string get_command() {
                    string command;
                    if (connections[id_].input_file->is_open() && getline(*connections[id_].input_file, command)) {
                        command += "\n";
                        write_command_output(command);
                        if (command.find("exit") != string::npos) {
                            connections[id_].input_file->close();
                        }
                    }
                    return command;
                }

                void close_session() {
                    if (connections[id_].input_file->is_open()) {
                        connections[id_].input_file->close();
                    }
                    connections[id_].sock->close();
                    delete connections[id_].sock;
                    delete connections[id_].input_file;
                    connections[id_].sock = nullptr;
                    connections[id_].input_file = nullptr;
                }

                string html_escape(string content) {
                boost::replace_all(content, "&", "&amp;");
                boost::replace_all(content, "\"", "&quot;");
                boost::replace_all(content, "\'", "&apos;");
                boost::replace_all(content, "<", "&lt;");
                boost::replace_all(content, ">", "&gt;");
                boost::replace_all(content, "\n", "&NewLine;");
                boost::replace_all(content, "\r", "");
                boost::replace_all(content, " ", "&nbsp;");
                return content;
            }

                int id_;
                tcp::socket socket_;
                shared_ptr<tcp::socket> web_socket_;
                tcp::resolver resolver_;
        };

        class WebSession : public enable_shared_from_this<WebSession> {
            public:
                WebSession(tcp::socket socket, boost::asio::io_context& io_context)
                    : socket_(move(socket)), io_context_(io_context) {}

                void start() {
                    do_read();
                }

            private:
                void do_read() {
                    auto self(shared_from_this());
                    socket_.async_read_some(
                        boost::asio::buffer(data_, max_length),
                        [this, self](boost::system::error_code ec, size_t length) {
                            if (!ec) {
                                parse_http_request();
                                do_write("HTTP/1.1 200 OK\r\n");
                                if (env_.PATH_INFO == "/panel.cgi") {
                                    do_write(generate_panel());
                                } else if (env_.PATH_INFO == "/console.cgi") {
                                    parse_query_string();
                                    do_write(generate_console());
                                    make_connections();
                                } else {
                                    cerr << "Invalid path: " << env_.PATH_INFO << endl;
                                }
                            } else {
                                cerr << "Read error: " << ec.message() << endl;
                            }
                        });
                }

                void do_write(const string& content) {
                    auto self(shared_from_this());
                    boost::asio::async_write(
                        socket_,
                        boost::asio::buffer(content.c_str(), content.length()),
                        [this, self](boost::system::error_code ec, size_t /*length*/) {
                            if (ec) {
                                cerr << "Write error: " << ec.message() << endl;
                            }
                        });
                }

                void parse_http_request() {
                    stringstream ss(data_);
                    ss >> env_.REQUEST_METHOD >> env_.REQUEST_URI >> env_.SERVER_PROTOCOL;
                    string temp;
                    ss >> temp;
                    if (temp == "Host:") {
                        ss >> env_.HTTP_HOST;
                    }

                    size_t pos = env_.REQUEST_URI.find("?");
                    if (pos != string::npos) {
                        env_.QUERY_STRING = env_.REQUEST_URI.substr(pos + 1);
                        env_.PATH_INFO = env_.REQUEST_URI.substr(0, pos);
                    } else {
                        env_.PATH_INFO = env_.REQUEST_URI;
                    }

                    env_.SERVER_ADDR = socket_.local_endpoint().address().to_string();
                    env_.SERVER_PORT = to_string(socket_.local_endpoint().port());
                    env_.REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
                    env_.REMOTE_PORT = to_string(socket_.remote_endpoint().port());
                }

                void parse_query_string() {
                    connections.clear();
                    connections.resize(MAX_CONNECTIONS, {0, "", "", "", "", nullptr, nullptr, {}});

                    string idx, value;
                    istringstream stream(env_.QUERY_STRING);
                    int id;
                    while (getline(stream, idx, '=')) {
                        getline(stream, value, '&');
                        if (idx.empty() || value.empty()) continue;
                        id = idx[1] - '0';
                        if (id < 0 || id >= MAX_CONNECTIONS) continue;

                        if (idx[0] == 'h') connections[id].host = value;
                        else if (idx[0] == 'p') connections[id].port = value;
                        else if (idx[0] == 'f') connections[id].file = value;
                    }

                    // Initialize resources for valid connections
                    for (int i = 0; i < MAX_CONNECTIONS; i++) {
                        if (!connections[i].host.empty() && !connections[i].port.empty() && !connections[i].file.empty()) {
                            connections[i].id = i;
                            connections[i].sock = new tcp::socket(io_context_);
                            connections[i].input_file = new ifstream(TEST_CASE_DIR + connections[i].file, ios::in);
                            if (connections[i].input_file->fail()) {
                                cerr << "Failed to open file: " << connections[i].file << endl;
                                delete connections[i].input_file;
                                delete connections[i].sock;
                                connections[i].input_file = nullptr;
                                connections[i].sock = nullptr;
                            }
                        }
                    }
                }

                void make_connections() {
                    auto web_socket = make_shared<tcp::socket>(move(socket_));
                    for (auto& conn : connections) {
                        if (conn.sock && conn.input_file) {
                            make_shared<ClientSession>(conn.id, io_context_, web_socket)->start();
                        }
                    }
                }

                string generate_panel() {
                    ostringstream oss;
                    oss << "Content-Type: text/html\r\n\r\n";
                    oss << R"(
                        <!DOCTYPE html>
                        <html lang="en">
                        <head>
                            <title>NP Project 3 Panel</title>
                            <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css" integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2" crossorigin="anonymous"/>
                            <link href="https://fonts.googleapis.com/css?family=Source+Code+Pro" rel="stylesheet"/>
                            <link rel="icon" type="image/png" href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"/>
                            <style>* { font-family: 'Source Code Pro', monospace; }</style>
                        </head>
                        <body class="bg-secondary pt-5">
                            <form action="console.cgi" method="GET">
                                <table class="table mx-auto bg-light" style="width: inherit">
                                    <thead class="thead-dark">
                                        <tr>
                                            <th scope="col">#</th>
                                            <th scope="col">Host</th>
                                            <th scope="col">Port</th>
                                            <th scope="col">Input File</th>
                                        </tr>
                                    </thead>
                                    <tbody>
                    )";

                    string host_menu, test_case_menu;
                    for (int i = 1; i <= MAX_NP_SERVERS; i++) {
                        host_menu += (boost::format("<option value=\"nplinux%1%.%2%\">nplinux%1%</option>")
                                    % i % DOMAIN).str();
                    }
                    for (int i = 1; i <= 5; i++) {
                        test_case_menu += (boost::format("<option value=\"t%1%.txt\">t%1%.txt</option>")
                                        % i).str();
                    }

                    for (int i = 0; i < MAX_CONNECTIONS; i++) {
                        oss << (boost::format(R"(
                            <tr>
                                <th scope="row" class="align-middle">Session %1%</th>
                                <td>
                                    <div class="input-group">
                                        <select name="h%2%" class="custom-select">
                                            <option></option>%3%
                                        </select>
                                        <div class="input-group-append">
                                            <span class="input-group-text">.%4%</span>
                                        </div>
                                    </div>
                                </td>
                                <td><input name="p%2%" type="text" class="form-control" size="5"/></td>
                                <td>
                                    <select name="f%2%" class="custom-select">
                                        <option></option>%5%
                                    </select>
                                </td>
                            </tr>
                        )") % (i + 1) % i % host_menu % DOMAIN % test_case_menu).str();
                    }

                    oss << R"(
                            <tr>
                                <td colspan="3"></td>
                                <td><button type="submit" class="btn btn-info btn-block">Run</button></td>
                            </tr>
                            </tbody>
                            </table>
                            </form>
                        </body>
                        </html>
                    )";
                    return oss.str();
                }

                string generate_console() {
                    ostringstream oss;
                    oss << "Content-Type: text/html\r\n\r\n";
                    oss << R"(
                        <!DOCTYPE html>
                        <html lang="en">
                        <head>
                            <meta charset="UTF-8"/>
                            <title>NP Project 3 Console</title>
                            <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css" integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2" crossorigin="anonymous"/>
                            <link href="https://fonts.googleapis.com/css?family=Source+Code+Pro" rel="stylesheet"/>
                            <link rel="icon" type="image/png" href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"/>
                            <style>
                                * { font-family: 'Source Code Pro', monospace; font-size: 1rem !important; }
                                body { background-color: #232731; }
                                pre { color: #D8DEE9; }
                                b { color: #a3be8c; }
                                th { color: #81A1C1; }
                            </style>
                        </head>
                        <body>
                            <table class="table table-dark table-bordered">
                                <thead><tr>
                    )";

                    for (const auto& conn : connections) {
                        if (conn.host.empty()) continue;
                        oss << (boost::format("<th scope=\"col\">%1%:%2%</th>")
                            % conn.host % conn.port).str();
                    }

                    oss << R"(
                                </tr></thead>
                                <tbody><tr>
                    )";

                    for (const auto& conn : connections) {
                        if (conn.host.empty()) continue;
                        oss << (boost::format("<td><pre id=\"s%1%\" class=\"mb-0\"></pre></td>")
                            % conn.id).str();
                    }

                    oss << R"(
                                </tr></tbody>
                            </table>
                        </body>
                        </html>
                    )";
                    return oss.str();
                }

                tcp::socket socket_;
                boost::asio::io_context& io_context_;
                enum { max_length = 4096 };
                char data_[max_length];
                Environment env_;
        };

        tcp::acceptor acceptor_;
        boost::asio::io_context& io_context_;
        static vector<ConnectionInfo> connections;
};

vector<ConnectionInfo> HTTPServer::connections;

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            cerr << "Usage: http_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        HTTPServer server(io_context, atoi(argv[1]));
        io_context.run();

    }
    catch (const exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}