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
#include <unordered_map>
#include <map>
#include <array>
#include <ctype.h>
using namespace std;

#define MAXBUFSIZE 15000
#define MAXUSER 30
#define PROMPT "% "

vector<string> words;
int fd_null;
map<pair<int, int>, array<int, 2>> UserPipe;    //{ [to , from] : [fd[0] , fd[1]] }

class cmd {
public:
    string command = "";
    string dir_name = "";
    int delay_line_num = -1; 
    int send_to = -1;
    int receive_from = -1;
    bool IsPipe = false;
    bool IsNumberPipe = false;
    bool IsErrPipe = false;
    bool UserPipe_rvc = false;
    bool UserPipe_send = false;
    bool IsRedir = false;
    void clear();
    void print();
};

vector<cmd> cmdlist;

void cmd::clear() {
    this->command = "";
    this->dir_name = "";
    this->delay_line_num = -1;
    this->send_to = -1;
    this->receive_from = -1;
    this->IsPipe = false;
    this->IsNumberPipe = false;
    this->IsErrPipe = false;
    this->IsRedir = false;
    this->UserPipe_rvc = false;
    this->UserPipe_send = false;
}

void cmd::print() { 
    cout << "Command: " << command << "\n"
         << "IsPipe: " << IsPipe << "\n"
         << "IsNumberPipe: " << IsNumberPipe << "\n"
         << "OtherPipe: " << IsErrPipe << "\n"
         << "DelayLineNum: " << delay_line_num << "\n"
         << "IsRedir: " << IsRedir << "\n"
         << "DirName: " << dir_name << "\n"
         << "UserPipe_rvc: " << UserPipe_rvc << "\n"
         << "UserPipe_send: " << UserPipe_send << "\n"
         << "send to: " << send_to << "\n"
         << "receive from: " << receive_from << "\n"
         << "----------------------\n";
}

class UserInfo{
    public:
        int user_id = -1;
        int fd = -1;
        string user_name = "(no name)";
        string port = "";
        bool Is_login = false;
        unordered_map<string, string> env;
        unordered_map<int, array<int, 2>> pipeMap; 
        int cmd_count = 0;
        void init();
};
void UserInfo::init() {
    this->user_id = -1;
    this->fd = -1;
    this->user_name = "(no name)";
    this->port = "";
    this->Is_login = false;
    this->env["PATH"]="bin:.";
    this->cmd_count = 0;
    pipeMap.clear(); 
}

vector<UserInfo> userList(MAXUSER + 1);

const string Welcome_Message =
    "****************************************\n"
    "** Welcome to the information server. **\n"
    "****************************************\n";

void sig_child(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {}
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

void broadcast(const string &msg) {
    for (int i = 1; i <= MAXUSER; i++) {
        if (userList[i].Is_login) {
            write(userList[i].fd, msg.c_str(), msg.size());
        }
    }
}

void update_pipeMap(UserInfo *user){
    unordered_map<int, array<int, 2>> new_map;
    for (const auto& pair : user->pipeMap) {
        int key = pair.first;
        array<int, 2> value = pair.second;
        if (key > 0) {
            new_map.emplace(key - 1, value);
        } else {
            //cout << "close: " << value[0] << "  " << value[1]<< endl;
            close(value[0]);
            close(value[1]);
        }
    }
    user->pipeMap = move(new_map);
}

void buildin_commands(UserInfo *user) {
    if (words[0] == "setenv") {
        user->env[words[1].c_str()] =  words[2].c_str();
        if (setenv(words[1].c_str(), words[2].c_str(), 1) != 0) {
            std::cerr << "Error: setenv failed." << std::endl;
        }
    } else if (words[0] == "printenv") {
        const char *pathvar = getenv(words[1].c_str());
        if (pathvar != NULL){
            string msg = pathvar + string("\n");
            write(user->fd, msg.c_str(), msg.size());
        }      
    }
    else if(words[0] == "who"){
        string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
        for (int i = 1; i <= MAXUSER; i++) {
            if (userList[i].Is_login) {
                msg += to_string(userList[i].user_id) + "\t" + userList[i].user_name + "\t" + userList[i].port;
                if (i == user->user_id) {
                    msg += "\t<-me";
                }
                msg += "\n";
            }
        }
        write(user->fd, msg.c_str(), msg.size());
    }
    else if(words[0] == "tell"){
        int rcv_id = stoi(words[1]);
        string msg = "";
        
        if (userList[rcv_id].Is_login) {
            msg += "*** " + user->user_name + " told you ***: ";
            for (int i = 2; i < words.size(); i++) {
                msg += words[i];
                if (i != words.size() - 1) {
                    msg += " ";
                }
            }
            msg += "\n";
            write(userList[rcv_id].fd, msg.c_str(), msg.size());
        }
        else {
            msg += "*** Error: user #" + to_string(rcv_id) + " does not exist yet. ***\n";
            write(user->fd, msg.c_str(), msg.size());
        }
    }
    else if (words[0] == "yell") {
        string msg = "*** " + user->user_name + " yelled ***: ";
        for (int i = 1; i < words.size(); i++) {
            msg += words[i];
            if (i != words.size() - 1) {
                msg += " ";
            }
        }
        msg += "\n";
        broadcast(msg);
    }
    else if (words[0] == "name") {
        bool foundName = false;
        string msg = "";
        for (const auto& account : userList) {
            if (account.user_name == words[1] && account.Is_login) {
                msg +=  "*** User '"  + words[1] + "' already exists. ***\n";
                write(user->fd, msg.c_str(), msg.size());
                foundName = true;
                break;
            }
        }        
        if (!foundName) {
            user->user_name = words[1];
            msg += "*** User from " + user->port + " is named '" + user->user_name + "'. ***\n";
            broadcast(msg);
        }
    }

    update_pipeMap(user); 
}

bool check_userpipes(int which, int num){
    for(auto &cmd: cmdlist){
        if(cmd.UserPipe_rvc || cmd.UserPipe_send){
            //send
            if(which == 0){
                cmd.UserPipe_send = true;
                cmd.send_to = num;
            }
            if(which == 1){
                cmd.UserPipe_rvc = true;
                cmd.receive_from = num;
            }
            return true;
        }
    }

    return false;
}


void create_cmdlist() {
    cmd tmp;
    for (string& word : words) {
        if (word == "|") { 
            if(tmp.command.empty()){
                cmdlist.back().IsPipe = true;
                continue;
            }
            tmp.IsPipe = true;
            cmdlist.push_back(tmp);
            tmp.clear();
        } 
        else if (word[0] == '|' && word.size() > 1) {
            if(tmp.command.empty()){
                cmdlist.back().IsNumberPipe = true;
                cmdlist.back().delay_line_num = stoi(word.substr(1)); 
                continue;
            }
            tmp.IsNumberPipe = true;
            tmp.delay_line_num = stoi(word.substr(1)); 
            cmdlist.push_back(tmp);
            tmp.clear();
        } 
        else if (word[0] == '!' && word.size() > 1) {
            if(tmp.command.empty()){
                cmdlist.back().IsErrPipe = true;
                cmdlist.back().delay_line_num = stoi(word.substr(1)); 
                continue;
            }
            tmp.IsErrPipe = true;
            tmp.delay_line_num = stoi(word.substr(1));  
            cmdlist.push_back(tmp);
            tmp.clear();
        }
        else if (word == ">") {
            if(tmp.command.empty()){
                cmdlist.back().IsRedir = true;
                continue;
            }
            tmp.IsRedir = true;
        }
        else if ( word[0] == '<' && word.size()>1){
            if(tmp.command.empty() && check_userpipes(1,stoi(word.substr(1))))    continue;
            tmp.UserPipe_rvc = true;
            tmp.receive_from = stoi(word.substr(1));  
            cmdlist.push_back(tmp);
            tmp.clear();
        }
        else if ( word[0] == '>' && word.size()>1){
            if(tmp.command.empty() && check_userpipes(0,stoi(word.substr(1))))   continue;
            tmp.UserPipe_send = true;
            tmp.send_to = stoi(word.substr(1));  
            cmdlist.push_back(tmp);
            tmp.clear();
        }
        else {
            if (!tmp.IsRedir) {
                if (!tmp.command.empty()) {
                    word.insert(word.begin(), ' ');
                    tmp.command.append(word);
                } else {
                    tmp.command = word;
                }
            } 
            else {
                if(tmp.command.empty()){
                    cmdlist.back().dir_name = word;
                    continue;
                }
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

int getUserIndex(int fd) {
    for (int i = 1; i <= MAXUSER; i++) {
        if (userList[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

void process_commands(UserInfo *user, const string &input) {
    int prev_pipe[2] = {user->fd, user->fd}; // Track previous pipe for regular pipes

    for (int i = 0; i < cmdlist.size(); i++) {
        cmd &currentcmd = cmdlist[i];

        int pipeNum = 0;
        if (currentcmd.UserPipe_rvc ) {
            //cout << "receive" << endl;
            pair<int, int> key = {user->user_id, currentcmd.receive_from}; // {to_id, from_id}
            if (!userList[currentcmd.receive_from].Is_login) {   // sender not login
                //cout << "in: " << prev_pipe[0]  << " out: " << prev_pipe[1]  << endl;
                string msg = "*** Error: user #" + to_string(currentcmd.receive_from) + " does not exist yet. ***\n";
                write(user->fd, msg.c_str(), msg.size());
                prev_pipe[0]  = fd_null;
            } 
            else if (UserPipe.count(key) == 0) {    // user pipe not exist
                //cout << "in: " << prev_pipe[0]  << " out: " << prev_pipe[1]  << endl;
                string msg = "*** Error: the pipe #" + to_string(currentcmd.receive_from) + "->#" + to_string(user->user_id) + " does not exist yet. ***\n";
                write(user->fd, msg.c_str(), msg.size());
                prev_pipe[0]  = fd_null;
            } 
            else {
                prev_pipe[0] = UserPipe[key][0];
                prev_pipe[1] = UserPipe[key][1];
                UserPipe.erase(key);  
                string msg = "*** " + user->user_name + " (#" + to_string(user->user_id) + ") just received from " +
                             userList[currentcmd.receive_from].user_name + " (#" + to_string(currentcmd.receive_from) + ") by '" + input + "' ***\n";
                broadcast(msg);
                //cout << "in: " << prev_pipe[0]  << " out: " << prev_pipe[1] << endl;
            }
        }

         // user pipe , send to
         if (currentcmd.UserPipe_send) {

            if (!userList[currentcmd.send_to].Is_login || currentcmd.send_to < 0 || currentcmd.send_to > 30) {
                string msg = "*** Error: user #" + to_string(currentcmd.send_to) + " does not exist yet. ***\n";
                write(user->fd, msg.c_str(), msg.size());
                prev_pipe[1] = fd_null;
                //cout << "hi" << endl;
                //continue;
            } 
            else {
                pair<int, int> key = {currentcmd.send_to, user->user_id}; // {to_id, from_id}
                if (UserPipe.count(key)) {
                    string msg = "*** Error: the pipe #" + to_string(user->user_id) + "->#" + to_string(currentcmd.send_to) + " already exists. ***\n";
                    write(user->fd, msg.c_str(), msg.size());
                    prev_pipe[1] = fd_null;
                    //cout << "hi" << endl;
                    //continue;
                } 
                else {
                    //cout << "hi2" << endl;
                    array<int, 2> pipe_fd;
                    pipe(pipe_fd.data());
                    //cout << "create user pipe fd[0] = " << pipe_fd[0] << " fd[1] = " << pipe_fd[1] << endl;
                    UserPipe[key] = pipe_fd;
                    string msg = "*** " + user->user_name + " (#" + to_string(user->user_id) + ") just piped '" + input + "' to " +
                                 userList[currentcmd.send_to].user_name + " (#" + to_string(currentcmd.send_to) + ") ***\n";
                    broadcast(msg);
                    //cout << "in: " << pipe_fd[0]  << " out: " << pipe_fd[1]  << endl;
                }
            }
        }

        // Check for input from a previous numbered pipe (|N) when N reaches 0
        if (user->pipeMap.count(0)) {
            //cout << "in pipmap" << endl;
            prev_pipe[0] = user->pipeMap[0][0];
            prev_pipe[1] = user->pipeMap[0][1];
            user->pipeMap.erase(0); // Remove pipe used as input
        } 
        // Handle pipes (regular |, numbered |N, or error !N)
        if (currentcmd.IsPipe || currentcmd.IsNumberPipe || currentcmd.IsErrPipe) {
            pipeNum = currentcmd.IsPipe ? 0 : currentcmd.delay_line_num;
            if (user->pipeMap.count(pipeNum) == 0) {
                array<int, 2> pipe_fd;
                if (pipe(pipe_fd.data()) < 0) {
                    perror("Pipe creation failed");
                    continue;
                }
                user->pipeMap.emplace(pipeNum, pipe_fd);
                //cout << "create pipe " << pipe_fd[0] << " " << pipe_fd[1] << endl;
            }
        }

        // Execute the command with proper file descriptors
        pid_t pid;
        while ((pid = fork()) == -1) {
            waitpid(-1, NULL, 0);
        }

        if (pid == 0) {
            
            if(prev_pipe[1] == fd_null){
                //cout << "null" << endl;
                dup2(fd_null, STDIN_FILENO);
                dup2(fd_null, STDOUT_FILENO);
                dup2(fd_null, STDERR_FILENO);
            }
            else{
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);

                if (currentcmd.IsPipe) {
                    close(user->pipeMap[0][0]);
                    dup2(user->pipeMap[0][1], STDOUT_FILENO);
                    close(user->pipeMap[0][1]);
                } 
                else if (currentcmd.IsNumberPipe || currentcmd.IsErrPipe) {
                    int pipeNum = currentcmd.delay_line_num;
                    close(user->pipeMap[pipeNum][0]);
                    dup2(user->pipeMap[pipeNum][1], STDOUT_FILENO);
                    if (currentcmd.IsErrPipe) {
                        dup2(user->pipeMap[pipeNum][1], STDERR_FILENO);
                    }
                    close(user->pipeMap[pipeNum][1]);
                }
                else if (currentcmd.UserPipe_send){
                    pair<int, int> key = {currentcmd.send_to, user->user_id};
                    dup2(UserPipe[key][1], STDOUT_FILENO);
                    dup2(UserPipe[key][1], STDERR_FILENO);
                    close(UserPipe[key][1]);
                }
            }
            
            if (currentcmd.IsRedir) {
                int fd = open(currentcmd.dir_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("Open file failed");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            vector<char*> args = parse_command(currentcmd.command);
            execvp(args[0], args.data());
            cerr << "Unknown command: [" << args[0] << "]." << endl;
            exit(1);
        } else { // Parent process
            if (prev_pipe[1] != user->fd && prev_pipe[1] != fd_null) { 
                //cout << "close" << endl;
                close(prev_pipe[0]);
                close(prev_pipe[1]);
                prev_pipe[0] = user->fd;
                prev_pipe[1] = user->fd;
            }

            if (currentcmd.IsPipe) {
                prev_pipe[0] = user->pipeMap[0][0];
                prev_pipe[1] = user->pipeMap[0][1];
                user->pipeMap.erase(0); 
            }

            int status;
            if (!currentcmd.IsPipe && !currentcmd.IsNumberPipe && !currentcmd.IsErrPipe) {
                waitpid(pid, &status, 0);
            } else {
                usleep(2000);
                while (waitpid(-1, NULL, WNOHANG) > 0); 
            }

            if (!currentcmd.IsPipe) { 
                update_pipeMap(user);
            }
        }
    }
}

int shell(int fd){
    // prevent zombie process
    signal(SIGCHLD, sig_child);

    char buf[MAXBUFSIZE];
    memset(buf, 0, sizeof(buf));
    int n = read(fd, buf, sizeof(buf));
    if (n == 0) { // client disconnected
        return -1;
    }
    else if (n < 0) { // read error
        cerr << "Error: failed to read from client" << endl;
    }

    string input(buf);
    input.erase(input.find_last_not_of(" \n\r\t") + 1); // remove trailing whitespace
    input_to_words(input);

    if( input.empty()){
        write(fd, PROMPT, strlen(PROMPT));
        return 0;
    }

    if( words[0] == "exit"){
        return -1;
    }

    // Redirect stdout, stderr
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    int userIndex = getUserIndex(fd);
    UserInfo *user = &userList.at(userIndex);

    // Clear the environment variables
    clearenv();
    // Set the environment variables for the user
    for (auto &env : user->env) {
        setenv(env.first.c_str(), env.second.c_str(), 1);
    }

    if( words[0] == "setenv" || words[0] == "printenv" || words[0] == "yell" || words[0] == "who" || words[0] == "tell" || words[0] == "name"){
        buildin_commands(user);
        write(fd, PROMPT, strlen(PROMPT)); 
        return 0;
    }

    create_cmdlist();
    //for( auto &i: cmdlist) i.print();
    //usleep(200);
    process_commands(user, input);

    // Print the command line prompt
    write(fd, PROMPT, strlen(PROMPT)); // %
    words.clear();
    cmdlist.clear();
    
    return 0;
}

class Server {
    private:
        int msock;              // Master socket
        int nfds;               // Max number of file descriptors
        fd_set afds;            // Active file descriptor set
        fd_set rfds;
        int storeStd[3];        // Stored stdin, stdout, stderr

        // Private helper function for passive TCP setup
        int TCP_Server(int port) {
            int serverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocketfd < 0) {
                cerr << "Error: server can't open stream socket" << endl;
            }

            // Set up the server address
            struct sockaddr_in serverAddr;
            // Configure settings of the server address struct
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);
            serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            memset(serverAddr.sin_zero, 0, sizeof(serverAddr.sin_zero));

            // Set socket to be reusable
            int optval = 1;
            if (setsockopt(serverSocketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
                cerr << "Error: server can't set socket to be reusable" << endl;
            }

            // Bind the socket to the server address
            if (bind(serverSocketfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
                cerr << "Error: server can't bind local address" << endl;
            }

            // Listen on the socket
            if (listen(serverSocketfd, MAXUSER) < 0) {
                cerr << "Error: server can't listen on socket" << endl;
            }

            return serverSocketfd;
        }

    public:
        Server(int port) {
            /* initial fd, avoiding error so using /dev/null */
            fd_null = open("/dev/null", O_RDWR);

            /* Create a TCP socket server and get its file descriptor (msock) */ 
            msock = TCP_Server(port);
            // int msock = passiveTCP(7000); 

            nfds = FD_SETSIZE; // 1024
            FD_ZERO(&afds); // clear active fd
            FD_SET(msock, &afds);   // add msock in active fd

            /* set env */
            for (int i = 1; i <= MAXUSER; i++) {
                userList[i].init();
            }

            // Store stdin, stdout, stderr
            
            storeStd[0] = dup(STDIN_FILENO);
            storeStd[1] = dup(STDOUT_FILENO);
            storeStd[2] = dup(STDERR_FILENO);
        }
        void Login();
        void Logout(int fd);
        void run();

};

void Server::Login(){
    /* Accept the new connection , create client socket*/ 
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int ssock = accept(msock, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (ssock < 0) {
        cerr << "Failed to accept the new connection" << endl;
    }

    /* Add the new client socket to the file descriptor set */ 
    FD_SET(ssock, &afds);

    // Get the client IP address and port
    char ipBuf[INET_ADDRSTRLEN];
    string ip = inet_ntoa(clientAddr.sin_addr);
    string clientIP = inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(clientIP));
    string clientPort = to_string(ntohs(clientAddr.sin_port));

    /* print out welcome message to client */
    write(ssock, Welcome_Message.c_str(), Welcome_Message.size());

    // Find the first available user slot
    for (int idx = 1; idx <= MAXUSER; idx++) {
        if (!userList[idx].Is_login) {
            userList[idx].Is_login = true;
            userList[idx].user_id = idx;
            userList[idx].port = clientIP + ":" + clientPort;
            userList[idx].fd = ssock;

            string msg = "*** User '" + userList[idx].user_name + "' entered from " + userList[idx].port + ". ***\n";
            broadcast(msg);
            break;
        }
    }
    /* print prompt after broadcast welcome message */
    write(ssock, PROMPT, strlen(PROMPT)); 
}

void Server::Logout(int fd) {
    int userIndex = getUserIndex(fd);
    if (userIndex != -1) {
        userList[userIndex].Is_login = false;
        string msg = "*** User '" + userList[userIndex].user_name + "' left. ***\n";
        broadcast(msg);

        for (auto it = UserPipe.begin(); it != UserPipe.end();) {
            pair<int, int> key = it->first;
            int to_id = key.first;
            int from_id = key.second;
            if (from_id == userIndex || to_id == userIndex) {
                close(it->second[0]);
                close(it->second[1]);
                it = UserPipe.erase(it);
            } else {
                it++;
            }
        }

        userList[userIndex].init();
    }
}

void Server::run() {
    while (true) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, NULL, NULL, NULL) < 0) {
            if (errno != EINTR) {
                cerr << "Error in select, errno: " << errno << endl;
            }
            continue;
        }

        if (FD_ISSET(msock, &rfds)) {
            Login();
        }

        for (int fd = 0; fd < nfds; ++fd) {
            if (fd != msock && FD_ISSET(fd, &rfds)) {
                int status = shell(fd); 
                if (status == -1) {
                    Logout(fd);
                    close(fd);
                    FD_CLR(fd, &afds);
                }
            }
        }

        // Restore stdin, stdout, stderr
        dup2(storeStd[0], STDIN_FILENO);
        dup2(storeStd[1], STDOUT_FILENO);
        dup2(storeStd[2], STDERR_FILENO);
    }
}

int main(int argc, char *argv[]){
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        return 1;
    }
    init_npshell();

    Server server(atoi(argv[1]));
    server.run();

    return 0;
}
