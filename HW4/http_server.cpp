#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>
#include <sys/wait.h>
#include <unordered_map>

using namespace boost::asio;
using namespace std;
using boost::asio::ip::tcp;
const string HTTP_OK = "HTTP/1.1 200 OK\r\n";

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

class ClientSession : public std::enable_shared_from_this<ClientSession> {
    public:
        ClientSession(tcp::socket socket) : socket_(std::move(socket)) {}   // copy socket to socket_
  
        void start() {
            do_read();
        }
  
    private:
        tcp::socket socket_;
        enum { max_length = 1024 };
        char content_[max_length];
        Environment env;

        void do_read() {
            auto self(shared_from_this());
            socket_.async_read_some(
                buffer(content_, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if (!ec) {
                        parseRequest();
                        createResponse();
                    }
                });
        }
  
        void parseRequest(){
            stringstream ss(content_);
            ss >> env.REQUEST_METHOD >> env.REQUEST_URI >> env.SERVER_PROTOCOL;

            string token;
            ss >> token;
            if (token == "Host:") {
                ss >> env.HTTP_HOST;
            }

            // Extract query string
            size_t pos = env.REQUEST_URI.find("?");
            if (pos != string::npos) {
                env.QUERY_STRING = env.REQUEST_URI.substr(pos + 1);
                env.PATH_INFO = env.REQUEST_URI.substr(0, pos);
            }
            else {
                env.PATH_INFO = env.REQUEST_URI;
            }

            env.SERVER_ADDR = socket_.local_endpoint().address().to_string();
            env.SERVER_PORT = to_string(socket_.local_endpoint().port());
            env.REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
            env.REMOTE_PORT = to_string(socket_.remote_endpoint().port());
        }
        void setEnv(){
            unordered_map<string, string> env_map = {
                {"REQUEST_METHOD", env.REQUEST_METHOD},
                {"REQUEST_URI", env.REQUEST_URI},
                {"QUERY_STRING", env.QUERY_STRING},
                {"SERVER_PROTOCOL", env.SERVER_PROTOCOL},
                {"HTTP_HOST", env.HTTP_HOST},
                {"SERVER_ADDR", env.SERVER_ADDR},
                {"SERVER_PORT", env.SERVER_PORT},
                {"REMOTE_ADDR", env.REMOTE_ADDR},
                {"REMOTE_PORT", env.REMOTE_PORT}
        
            };
        
            for (const auto& idx : env_map) {
                setenv(idx.first.c_str(), idx.second.c_str(), 1);
            }
        }
        void createResponse(){
            pid_t pid = fork();
            if (pid == -1){
                cerr << "fork: " << strerror(errno) << std::endl;
                exit(0);
            }
            
            if (pid == 0) {
                setEnv();
                dup2(socket_.native_handle(), STDIN_FILENO);
                dup2(socket_.native_handle(), STDOUT_FILENO);
                socket_.close();

                cout << HTTP_OK << flush;

                string path = "." + env.PATH_INFO;
                cout << env.PATH_INFO << endl;
                cout << path << endl;
                if (execlp(path.c_str(), path.c_str(), NULL) == -1) {
                    cout << "Error executing script" << endl;
                }
            }
            else {
                socket_.close();
            }
        }

};


class Server {
    public:
      Server(io_context &io_context, short port)
          : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) { //initial accpert , and call do_accept()
            initiateAccept();
      }
  
    private:
      void initiateAccept() {
          acceptor_.async_accept(
              [this](boost::system::error_code ec, tcp::socket socket) {
                  if (!ec) {
                      make_shared<ClientSession>(move(socket))->start();    // make_shared to create ClientSession managed by shared_ptr
                  }
  
                  initiateAccept();
              });
      }
  
      tcp::acceptor acceptor_;
};

void sig_child(int signal) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {}
}

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }
        signal(SIGCHLD, sig_child);

        io_context io_context;
        Server s(io_context, atoi(argv[1]));
        io_context.run();
    } 
    catch (exception &e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}