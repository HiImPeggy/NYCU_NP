#include <iostream>
#include <stdlib.h>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h> 
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <array>
#include <unordered_map>
using namespace std;

vector<string> words;

class cmd {
public:
    string command = "";
    string dir_name = "";
    int delay_line_num = -1; 
    bool IsPipe = false;
    bool IsNumberPipe = false;
    bool otherpipe = false;
    bool IsRedir = false;
    void clear();
    void print();
};

vector<cmd> cmdlist;
unordered_map<int, array<int, 2>> pipeMap; 

void cmd::clear() {
    this->command = "";
    this->dir_name = "";
    this->delay_line_num = -1;
    this->IsPipe = false;
    this->IsNumberPipe = false;
    this->otherpipe = false;
    this->IsRedir = false;
}

void cmd::print() { 
    cout << "Command: " << command << "\n"
         << "IsPipe: " << IsPipe << "\n"
         << "IsNumberPipe: " << IsNumberPipe << "\n"
         << "OtherPipe: " << otherpipe << "\n"
         << "DelayLineNum: " << delay_line_num << "\n"
         << "IsRedir: " << IsRedir << "\n"
         << "DirName: " << dir_name << "\n"
         << "----------------------\n";
}

void sig_child(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {}
}

void update_pipeMap() {
    unordered_map<int, array<int, 2>> new_map;
    for (const auto& pair : pipeMap) {
        int key = pair.first;
        array<int, 2> value = pair.second;
        if (key > 0) {
            new_map.emplace(key - 1, value);
        } else {
            close(value[1]);
        }
    }
    pipeMap = move(new_map);
}

void init_npshell() {
    clearenv();
    setenv("PATH", "bin:.", 1);
    signal(SIGCHLD, sig_child);
}

void input_to_words(string input) {
    stringstream ss(input);
    string tmp;
    words.clear();
    while (ss >> tmp) {
        words.push_back(tmp);
    }
}

void buildin_commands() {
    if (words[0] == "setenv") {
        if (setenv(words[1].c_str(), words[2].c_str(), 1) != 0) {
            // std::cerr << "Error: setenv failed." << std::endl;
        }
    } else if (words[0] == "printenv") {
        const char *pathvar = getenv(words[1].c_str());
        if (pathvar != NULL)    
            cout << pathvar << endl;
    }
    update_pipeMap(); 
}

void create_cmdlist() {
    cmd tmp;
    for (string& word : words) {
        if (word == "|") { 
            tmp.IsPipe = true;
            cmdlist.push_back(tmp);
            tmp.clear();
        } 
        else if (word[0] == '|' && word.size() > 1) {
            tmp.IsNumberPipe = true;
            tmp.delay_line_num = stoi(word.substr(1)); 
            cmdlist.push_back(tmp);
            tmp.clear();
        } 
        else if (word[0] == '!' && word.size() > 1) {
            tmp.otherpipe = true;
            tmp.delay_line_num = stoi(word.substr(1));  
            cmdlist.push_back(tmp);
            tmp.clear();
        }
        else if (word == ">") {
            tmp.IsRedir = true;
        }
        else {
            if (!tmp.IsRedir) {
                if (!tmp.command.empty()) {
                    word.insert(word.begin(), ' ');
                    tmp.command.append(word);
                } else {
                    tmp.command = word;
                }
            } else {
                tmp.dir_name = word;
                cmdlist.push_back(tmp);
                tmp.clear();
            }
        }
    }
    if (!tmp.command.empty()) {
        cmdlist.push_back(tmp);
    }
}

vector<char*> parse_command(const string& command) {
    vector<char*> args;
    stringstream ss(command);
    string tmp;
    while (ss >> tmp) {
        args.push_back(strdup(tmp.c_str()));
    }
    args.push_back(nullptr);
    return args;
}

void execute_cmdlist() {
    int prev_pipe[2] = {-1, -1}; 

    for (int i = 0; i < cmdlist.size(); i++) {
        cmd& current = cmdlist[i];

        if (pipeMap.count(0)) {
            prev_pipe[0] = pipeMap[0][0];
            prev_pipe[1] = pipeMap[0][1];
            pipeMap.erase(0); 
        }

        if (current.IsPipe || current.IsNumberPipe || current.otherpipe) {
            int pipeNum = current.IsPipe ? 0 : current.delay_line_num;
            if (pipeMap.count(pipeNum) == 0) { 
                array<int, 2> pipe_fd;
                pipe(pipe_fd.data());
                pipeMap.emplace(pipeNum, pipe_fd);
            }
        }

    FORK:
        pid_t pid = fork();
        if (pid < 0) {
            while (waitpid(-1, NULL, WNOHANG) > 0) {}
            goto FORK;
        }

        usleep(200);
        if (pid == 0) { 
            if (prev_pipe[0] != -1) { 
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }

            if (current.IsPipe) {
                close(pipeMap[0][0]);
                dup2(pipeMap[0][1], STDOUT_FILENO);
                close(pipeMap[0][1]);
            } else if (current.IsNumberPipe || current.otherpipe) {
                int pipeNum = current.delay_line_num;
                close(pipeMap[pipeNum][0]);
                dup2(pipeMap[pipeNum][1], STDOUT_FILENO);
                if (current.otherpipe) {
                    dup2(pipeMap[pipeNum][1], STDERR_FILENO);
                }
                close(pipeMap[pipeNum][1]);
            }

            if (current.IsRedir) {
                int fd = open(current.dir_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("Open file failed");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            vector<char*> args = parse_command(current.command);
            execvp(args[0], args.data());
            cerr << "Unknown command: [" << args[0] << "]." << endl;
            exit(1);
        } else { 
            if (prev_pipe[1] != -1) { 
                close(prev_pipe[0]);
                close(prev_pipe[1]);
                prev_pipe[0] = -1;
                prev_pipe[1] = -1;
            }

            if (current.IsPipe) {
                prev_pipe[0] = pipeMap[0][0];
                prev_pipe[1] = pipeMap[0][1];
                pipeMap.erase(0); 
            }

            int status;
            if (!current.IsPipe && !current.IsNumberPipe && !current.otherpipe) {
                waitpid(pid, &status, 0);
            } else {
                usleep(2000);
                while (waitpid(-1, NULL, WNOHANG) > 0); 
            }

            if (!current.IsPipe) { 
                update_pipeMap();
            }
        }
    }
}


void npsimple_shell(){
    string input;
    init_npshell();

    while(true){
        usleep(3000);    // avoid order wrong
        cout << "% ";
        getline(cin,input);
        //cout << input << endl;
        
        if( input.empty())    continue;

        if (cin.eof()) {
            //cout << "cin is eof" << endl;
            cout << endl;
            break;
        }

        /* seperate string: input , with space and store into vector: words(global var)*/
        input_to_words(input);

        /* check if the input is build-in command*/
        if( words[0] == "exit" ){
            //cout << "in exit" << endl;
            exit(0);
        }
        if( words[0] == "setenv" || words[0] == "printenv" ){
            //cout << "peintenv or setenv" << endl;
            buildin_commands();
            continue;
        }

        /* fill commands in cmdlist */
        create_cmdlist();
        execute_cmdlist();
        //printf("complete command\n");

        words.clear();
        cmdlist.clear();
    }
}

int main(int argc, char *argv[]){

    /* initial PATH to bin/ and ./ */
    init_npshell();

    int serverSocketfd; // master socket file descriptor
    int newSocketfd;    // slave socket file descriptor
    struct sockaddr_in serverAddr;
    struct sockaddr_in clientAddr;
    socklen_t addr_size = sizeof(sockaddr_in);

    // Create the socket
    serverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketfd < 0) {
        cerr << "Error: server can't open stream socket" << endl;
    }

    // Configure settings of the server address struct
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(argv[1])); // Convert argv[1] to integer
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(serverAddr.sin_zero, 0, sizeof(serverAddr.sin_zero));

    // Set socket to be reusable
    int optval = 1;
    if (setsockopt(serverSocketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        cerr << "Error: server can't set socket to be reusable" << endl;
    }

    // Bind the address struct to the socket
    if (bind(serverSocketfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error: server can't bind local address" << endl;
    }

    // Listen on the socket, with 5 max connection requests queued
    if (listen(serverSocketfd, 5) < 0) {
        cerr << "Error: server can't listen on socket" << endl;
    }

    while (true) {
        // Accept call creates a new socket for the incoming connection
        newSocketfd = accept(serverSocketfd, (struct sockaddr *)&clientAddr, &addr_size);
        if (newSocketfd < 0) {
            cerr << "Error: server can't accept client connection" << endl;
        }

        // Fork a child process to handle the new connection (Concurrent connection-oriented server)
        pid_t pid = fork();
        if (pid == 0) { // child process
            // Redirect stdin, stdout, stderr to the new socket
            dup2(newSocketfd, STDIN_FILENO);
            dup2(newSocketfd, STDOUT_FILENO);
            dup2(newSocketfd, STDERR_FILENO);
            close(newSocketfd);
            close(serverSocketfd);
            npsimple_shell();
        }
        else if (pid > 0) { // parent process
            close(newSocketfd);
        }
    }

    return 0;
}
