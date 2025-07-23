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
#include <sys/shm.h> 
#include <sys/stat.h>
using namespace std;

#define SHMKEY_USERINFO 2025
#define SHMKEY_MESSAGE 2026
#define SHMKEY_USERPIPEINFO 2027
#define PERMS 0666
#define MAXBUFSIZE 15000
#define MAXUSER 30
#define PROMPT "% "
#define MAX_FIFOPATH 100

vector<string> words;
int fd_null;

map<int, int> UserPipe_fd;

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
    bool append = false;
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
    this->append = false;
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

/* share memory for user and broadcast message*/
struct UserInfo{
    int user_id;
    int receive_from;
    char user_name[25];
    char port[25];
    bool Is_login; 
    pid_t pid;
};
struct BroadcastMsg {
    char msg[MAXBUFSIZE];
};

/*shared memory */
int shmid_user = -1;
int shmid_msg = -1;

int current_id;

UserInfo *userList;
BroadcastMsg *shm_broadcast;


const string Welcome_Message =
    "****************************************\n"
    "** Welcome to the information server. **\n"
    "****************************************\n";

void sig_child(int signo) {
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {}
}

void sig_terminated(int signo) {
    shmdt(userList);
    shmdt(shm_broadcast);
    shmctl(shmid_user, IPC_RMID, (struct shmid_ds *)0); // remove shared memory (user information)
    shmctl(shmid_msg, IPC_RMID, (struct shmid_ds *)0);  // remove shared memory (broadcast message)
    exit(0);
}

void sig_FIFO(int signo) {
    for (int i = 1; i <= MAXUSER; i++) {
        if (userList[i].Is_login && userList[i].receive_from != 0) {
            string file = "./user_pipe/" + to_string(userList[i].receive_from) + '/' + to_string(userList[i].user_id);
            //cout << file << endl;
            int fd = open(file.c_str(), O_RDONLY);
            if (fd < 0) {
                cerr << "Error: failed to open readfd for user pipe" << endl;
            }
            UserPipe_fd[userList[i].receive_from] = fd;
            userList[i].receive_from = 0;
            break;
        }
    }
}

void init_npshell() {
    clearenv();
    setenv("PATH", "bin:.", 1);
    signal(SIGCHLD, sig_child);
}

void sig_broadcast(int signo) {
    cout << shm_broadcast->msg << flush;
}

void broadcast(const string &msg) {
    memset(shm_broadcast->msg, 0, MAXBUFSIZE);
    strcpy(shm_broadcast->msg, msg.c_str());
    for (int idx = 1; idx <= MAXUSER; idx++) {
        if (userList[idx].Is_login) {
            kill(userList[idx].pid, SIGUSR1); // send SIGUSR1 to users, then call sig_broadcast
        }
    }
    usleep(3000);
}

void input_to_words(string input) {
    stringstream ss(input);
    string tmp;
    words.clear();
    while (ss >> tmp) {
        words.push_back(tmp);
    }
}

void deleteUserPipe(int id) {
    for (int i = 1; i <= MAXUSER; i++) {
        string file = "./user_pipe/" + to_string(id) + '/' + to_string(i);
        if (unlink(file.c_str()) < 0 && errno != ENOENT) { // ENOENT: No such file or directory
            cerr << "Error: failed to unlink user pipe" << endl;
        }
        file = "./user_pipe/" + to_string(i) + "/" + to_string(id);
        if (unlink(file.c_str()) < 0 && errno != ENOENT) {
            cerr << "Error: failed to unlink user pipe" << endl;
        }
    }
}

void init_userinfo(int idx) {
    userList[idx].Is_login = false;
    userList[idx].user_id = 0;
    userList[idx].receive_from = 0;
    strcpy(userList[idx].user_name, "(no name)");
    strcpy(userList[idx].port, "");
    userList[idx].pid = -1;
}

void Logout(int idx) {
    if (idx != -1) {
        userList[idx].Is_login = false; 
        string msg = "*** User '" + string(userList[idx].user_name) + "' left. ***\n";
        broadcast(msg);
        init_userinfo(idx);
        //TODO: delete user pipe
        deleteUserPipe(idx);
        shmdt(userList);    // detach share memory from this user
        shmdt(shm_broadcast);
        shmctl(shmid_user, IPC_RMID, (struct shmid_ds *)0); // remove shared memory (user information)
        shmctl(shmid_msg, IPC_RMID, (struct shmid_ds *)0);  // remove shared memory (broadcast message)
    }
}

void update_pipeMap(unordered_map<int, array<int, 2>> &pipeMap) {
    unordered_map<int, array<int, 2>> new_map;
    /*for (const auto& pair : pipeMap) {
        cout << pair.first << endl;
    }*/
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

void buildin_commands(UserInfo *user, unordered_map<int, array<int, 2>> &pipeMap) {
    if (words[0] == "setenv") {
        if (setenv(words[1].c_str(), words[2].c_str(), 1) != 0) {
            std::cerr << "Error: setenv failed." << std::endl;
        }
    } 
    else if (words[0] == "printenv") {
        const char *pathvar = getenv(words[1].c_str());
        if (pathvar != NULL){
            cout << pathvar << endl;
        }      
    }
    else if(words[0] == "who"){
        cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
        for (int i = 1; i <= MAXUSER; i++) {
            if (userList[i].Is_login) {
                cout << userList[i].user_id << "\t" << userList[i].user_name << "\t" << userList[i].port;
                if (i == user->user_id) {
                    cout << "\t<-me";
                }
                cout << "\n";
            }
        }
    }
    else if(words[0] == "tell"){
        int rcv_id = stoi(words[1]);
        string msg = "";
        
        if (userList[rcv_id].Is_login) {
            msg += "*** " + string(user->user_name) + " told you ***: ";
            for (int i = 2; i < words.size(); i++) {
                msg += words[i];
                if (i != words.size() - 1) {
                    msg += " ";
                }
            }
            msg += "\n";

            memset(shm_broadcast->msg, 0, MAXBUFSIZE);
            strcpy(shm_broadcast->msg, msg.c_str());
            kill(userList[rcv_id].pid, SIGUSR1);
        }
        else {
            cout << "*** Error: user #" + to_string(rcv_id) + " does not exist yet. ***\n";
        }
    }
    else if (words[0] == "yell") {
        string msg = "*** " + string(user->user_name) + " yelled ***: ";
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
        for (int i = 1; i <= MAXUSER; i++) {
            if (userList[i].Is_login && userList[i].user_name == words[1]) {
                cout << "*** User '" << words[1] << "' already exists. ***" << endl;
                foundName = true;
                break;
            }
        }    
        if (!foundName) {
            strcpy(user->user_name, words[1].c_str());
            string msg = "*** User from " + string(user->port) + " is named '" + user->user_name + "'. ***\n";
            broadcast(msg);
        }
    }

    update_pipeMap(pipeMap);
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
        else if (word[0] == '>' && word[1] == '>' && word.size()>1) {
            tmp.append = true;
            tmp.UserPipe_send = true;
            tmp.send_to = stoi(word.substr(2));  
            cmdlist.push_back(tmp);
            tmp.clear();
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

void process_commands(UserInfo *user, const string &input, unordered_map<int, array<int, 2>> &pipeMap) {
    int prev_pipe[2] = {-1,-1}; // Track previous pipe for regular pipes
    int dummy_rfd;

    for (int i = 0; i < cmdlist.size(); i++) {
        cmd &currentcmd = cmdlist[i];
        //currentcmd.print();
        int pipeNum = 0;
        string msg;
        int userpipe_write;
        if (currentcmd.UserPipe_rvc) {

            //cout << "in receive" << endl;
            if (!userList[currentcmd.receive_from].Is_login) {   // sender not login
                cout <<  "*** Error: user #" + to_string(currentcmd.receive_from) + " does not exist yet. ***\n";
                prev_pipe[0]  = fd_null;
            } 
            else if (UserPipe_fd.count(currentcmd.receive_from) == 0) {    // user pipe not exist
                //cout << "in: " << prev_pipe[0]  << " out: " << prev_pipe[1]  << endl;
                cout << "*** Error: the pipe #" + to_string(currentcmd.receive_from) + "->#" + to_string(user->user_id) + " does not exist yet. ***\n";
                prev_pipe[0]  = fd_null;
            } 
            else {
                prev_pipe[0] = UserPipe_fd[currentcmd.receive_from];
                //cerr << "in userpipe receive: " << prev_pipe[0] << endl;
                string file = "./user_pipe/" + to_string(currentcmd.receive_from) + '/' + to_string(user->user_id);
                //cout << file << endl;
                unlinkat(AT_FDCWD, file.c_str(), 0);
                UserPipe_fd.erase(currentcmd.receive_from);
                
                msg = "*** " + string(user->user_name) + " (#" + to_string(user->user_id) + ") just received from " +
                             userList[currentcmd.receive_from].user_name + " (#" + to_string(currentcmd.receive_from) + ") by '" + input + "' ***\n";
                broadcast(msg);
                //cout << "in: " << prev_pipe[0]  << " out: " << prev_pipe[1] << endl;
            }
        }

        // user pipe , send to
        if (currentcmd.UserPipe_send) {
            
            /* user not exist*/
            if (!userList[currentcmd.send_to].Is_login || currentcmd.send_to < 0 || currentcmd.send_to > 30) {
                prev_pipe[1] = fd_null;
                cout << "*** Error: user #" + to_string(currentcmd.send_to) + " does not exist yet. ***\n";
            } 
            else {
                string file = "./user_pipe/" + to_string(user->user_id) + '/' + to_string(currentcmd.send_to);
                // Ensure parent directories exist
                string parent_dir = "./user_pipe/" + to_string(user->user_id);
                if (mkdir("./user_pipe", 0777) < 0 && errno != EEXIST) {
                    perror("mkdir ./user_pipe failed");
                }
                if (mkdir(parent_dir.c_str(), 0777) < 0 && errno != EEXIST) {
                    perror("mkdir parent_dir failed");
                }
                
                //cout << file << endl;
                if (faccessat(AT_FDCWD, file.c_str(), W_OK, 0) == 0 && !currentcmd.append) { // user pipe already exists
                    prev_pipe[1] = fd_null;
                    cout << "*** Error: the pipe #" + to_string(user->user_id) + "->#" + to_string(currentcmd.send_to) + " already exists. ***\n";
                } 
                else {
                    //cout << file << endl;
                    
                    if(!currentcmd.append){
                        if (mkfifoat(AT_FDCWD, file.c_str(),0660)< 0 ) {
                            perror("mkfifo failed");
                        }
                    }
                   
                    userList[currentcmd.send_to].receive_from = user->user_id;
                    if (kill(userList[currentcmd.send_to].pid, SIGUSR2) < 0) {
                        perror("kill SIGUSR2 failed");
                    }

                    int write_fd = openat(AT_FDCWD, file.c_str(), O_WRONLY );
                    if (write_fd < 0) {
                        perror("open write_fd failed");
                    } 

                    userpipe_write = write_fd; 
                    //cerr << " in userpipe send: " << prev_pipe[1] << endl;

                    if (currentcmd.send_to  == user->user_id) {
                        UserPipe_fd[user->user_id] = openat(AT_FDCWD, file.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    } 
                    //cerr << "A: in userpipe send" << endl;
                    string msg = "*** " + string(user->user_name)+ " (#" + to_string(user->user_id) + ") just piped '" + input + "' to " +
                    userList[currentcmd.send_to].user_name + " (#" + to_string(currentcmd.send_to) + ") ***\n";
                    broadcast(msg);

                }
            }
        }
        
        if (pipeMap.count(0)) {
            prev_pipe[0] = pipeMap[0][0];
            prev_pipe[1] = pipeMap[0][1];
            pipeMap.erase(0); 
        }

        if (currentcmd.IsPipe || currentcmd.IsNumberPipe || currentcmd.IsErrPipe) {
            int pipeNum = currentcmd.IsPipe ? 0 : currentcmd.delay_line_num;
            if (pipeMap.count(pipeNum) == 0) { 
                array<int, 2> pipe_fd;
                pipe(pipe_fd.data());
                pipeMap.emplace(pipeNum, pipe_fd);
            }
        }

        // Execute the command with proper file descriptors
        pid_t pid;
        while ((pid = fork()) == -1) {
            waitpid(-1, NULL, 0);
        }

        if (pid == 0) { 

            if(prev_pipe[1] == fd_null && !currentcmd.UserPipe_rvc ){
                //cout << "null" << endl;
                dup2(fd_null, STDIN_FILENO);
                dup2(fd_null, STDOUT_FILENO);
                dup2(fd_null, STDERR_FILENO);
            }
            else{
                //cerr << "normal" << endl;
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);

                if (currentcmd.IsPipe) {
                    //cerr << "normal pipe" << endl;
                    close(pipeMap[0][0]);
                    dup2(pipeMap[0][1], STDOUT_FILENO);
                    close(pipeMap[0][1]);
                } 
                else if (currentcmd.IsNumberPipe || currentcmd.IsErrPipe) {
                    //cerr << "other pipe" << endl;
                    int pipeNum = currentcmd.delay_line_num;
                    close(pipeMap[pipeNum][0]);
                    dup2(pipeMap[pipeNum][1], STDOUT_FILENO);
                    if (currentcmd.IsErrPipe) {
                        dup2(pipeMap[pipeNum][1], STDERR_FILENO);
                    }
                    close(pipeMap[pipeNum][1]);
                }
                else if (currentcmd.UserPipe_send){
                    //cerr << "in userpipe send" << endl;
                    dup2(userpipe_write, STDOUT_FILENO);
                    dup2(userpipe_write, STDERR_FILENO);
                    close(userpipe_write);
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
        } else { 
            if (prev_pipe[1] != -1 && prev_pipe[1] != fd_null) { 
                //cerr << "close " << prev_pipe[0] << " and " << prev_pipe[1] << endl;
                close(prev_pipe[0]);
                close(prev_pipe[1]);
                prev_pipe[0] = -1;
                prev_pipe[1] = -1;
            }

            if(currentcmd.UserPipe_send){
                close(userpipe_write);
                close(prev_pipe[0]);
                prev_pipe[1] = -1;
            }

            if (currentcmd.IsPipe) {
                prev_pipe[0] = pipeMap[0][0];
                prev_pipe[1] = pipeMap[0][1];
                pipeMap.erase(0); 
            }

            int status;
            if (!currentcmd.IsPipe && !currentcmd.IsNumberPipe && !currentcmd.IsErrPipe  ){
                waitpid(pid, &status, 0);
            } else {
                usleep(2000);
                while (waitpid(-1, NULL, WNOHANG) > 0); 
            }

            if (!currentcmd.IsPipe) { 
                //cout << "enter update_pipeMap" << endl;
                update_pipeMap(pipeMap);
            }
        }
    }
}

int np_shell(int idx, unordered_map<int, array<int, 2>> &pipeMap) {
    // Get the current user
    UserInfo *user = &userList[idx];

    // Clear the environment variables
    clearenv();
    // Set the environment variables for the user
    setenv("PATH", "bin:.", 1);

    string input;

    while (true) {
        getline(cin, input);
        input.erase(input.find_last_not_of(" \n\r\t") + 1); // remove trailing whitespace

        input_to_words(input);

        if(input.empty()){
            cout << PROMPT; 
            continue;
        }

        if( words[0] == "exit"){
            Logout(idx);
            exit(0);
        }

        if( words[0] == "setenv" || words[0] == "printenv" || words[0] == "yell" || words[0] == "who" || words[0] == "tell" || words[0] == "name"){
            //cerr << "It's buildin function" << endl;
            buildin_commands(user,pipeMap);
            cout << PROMPT; 
            continue;
        }
        create_cmdlist();
        process_commands(user, input, pipeMap);

        // Print the command line prompt
        cout << PROMPT; // %
        
        words.clear();
        cmdlist.clear();
    }

    return 0;
}

class Server {
    private:
        int msock;              // Master socket
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

        void createSharedMemory() {
            // Create shared memory for user information
            shmid_user = shmget(SHMKEY_USERINFO, sizeof(UserInfo) * (MAXUSER + 1), PERMS | IPC_CREAT);
            if (shmid_user < 0) {
                cerr << "Error: failed to create shared memory for user information" << endl;
                exit(1);
            }
            //append userList to share memory: shmid_user
            userList = (UserInfo *)shmat(shmid_user, 0, 0);
            if (userList == (UserInfo *)-1) {
                cerr << "Error: failed to attach shared memory for user information" << endl;
                exit(1);
            }
        
            // Create shared memory for broadcast message
            shmid_msg = shmget(SHMKEY_MESSAGE, sizeof(BroadcastMsg), PERMS | IPC_CREAT);
            if (shmid_msg < 0) {
                cerr << "Error: failed to create shared memory for broadcast message" << endl;
                exit(1);
            }
            //append broadcast to share memory: shmid_msg
            shm_broadcast = (BroadcastMsg *)shmat(shmid_msg, 0, 0);
            if (shm_broadcast == (BroadcastMsg *)-1) {
                cerr << "Error: failed to attach shared memory for broadcast message" << endl;
                exit(1);
            }
        }

        pair <int, int> Login(int msock) {
            // Accept the new connection
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            int ssock = accept(msock, (struct sockaddr *)&clientAddr, &clientAddrLen);
            if (ssock < 0) {
                cerr << "Failed to accept the new connection" << endl;
            }
            //cerr << "Accepted new connection: " << ssock << endl;

            // Get the client IP address and port
            char ipBuf[INET_ADDRSTRLEN];
            string clientIP = inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(clientIP));
            string clientPort = to_string(ntohs(clientAddr.sin_port));
            string ipPort = clientIP + ":" + clientPort;
        
            // Find the first available user slot
            for (int i = 1; i <= MAXUSER; i++) {
                if (!userList[i].Is_login) {
                    userList[i].Is_login = true;
                    userList[i].user_id = i;
                    strcpy(userList[i].port, ipPort.c_str());
                    return {ssock, userList[i].user_id};
                }
            }
            return {-1, -1};
        }

    public:
        Server(int port) {
            /* initial fd, avoiding error so using /dev/null */
            fd_null = open("/dev/null", O_RDWR);

            signal(SIGCHLD, sig_child); 
            signal(SIGINT, sig_terminated);
            signal(SIGUSR1, sig_broadcast);
            signal(SIGUSR2, sig_FIFO);


            /* Create a TCP socket server and get its file descriptor (msock) */ 
            msock = TCP_Server(port);
            
            createSharedMemory();

            for (int i = 1; i <= MAXUSER; i++) {
                init_userinfo(i);
            }
        
            while (true) {
                auto login_result = Login(msock);
                int ssock = login_result.first;
                int userIndex = login_result.second;
            
                if (userIndex == -1 || ssock == -1) {
                    close(ssock);
                    continue;
                }
            
                pid_t pid;
                while ((pid = fork()) == -1) {
                    waitpid(-1, NULL, 0);
                }
            
                if (pid == 0) { // child process
                    close(msock);
                    dup2(ssock, STDIN_FILENO);
                    dup2(ssock, STDOUT_FILENO);
                    dup2(ssock, STDERR_FILENO);
                    close(ssock);
                
                    cout << Welcome_Message;
                    string msg = "*** User '" + string(userList[userIndex].user_name) + "' entered from " + userList[userIndex].port + ". ***\n";
                    broadcast(msg);
                    cout << PROMPT;
                    unordered_map<int, array<int, 2>> pipeMap; 
                    np_shell(userIndex, pipeMap);   
                }
                else {
                    userList[userIndex].pid = pid;
                    close(ssock);
                }
            
            }
        }
   
};

int main(int argc, char *argv[]){
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>" << endl;
        return 1;
    }
    init_npshell();

    Server server(atoi(argv[1]));

    return 0;
}
