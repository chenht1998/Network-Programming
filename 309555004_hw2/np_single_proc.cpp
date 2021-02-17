
#include "np_single_proc.h"
#include <cstdlib>  //atoi
#include <stdlib.h> //setenv
#include <unistd.h> //fork exec STDOUT_FILENO
#include <iostream> //cin cout
#include <fcntl.h>
#include <string.h>
#include <vector>

#include <fstream> //open(), for check file exit
#include <sstream> //stringstream

#include <sys/types.h>
#include <sys/wait.h> //wait
#include <sys/socket.h>
#include <sys/time.h> //select()
#include <netinet/in.h> //bind(), AF_INET
#include <arpa/inet.h> //inet_ntop()

using namespace std;

int pipe_end = 0;
int status;

bool debug_mode = false;


int main(int argc, char* argv[]) {
    int msockfd = setupServer(atoi(argv[1]));
    accept_new_connection(msockfd);
    return 0;
}

int setupServer(int port){
    int msockfd;
    struct sockaddr_in serv_addr;

    //open socket
    check(msockfd = socket(AF_INET, SOCK_STREAM,0), "server: can't open stream socket");


    //initial server_addr data
    // & bind local addr for client being able to send msg
    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    int flag = 1;
    if (setsockopt(msockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag)) < 0){
        cerr << "reuse addr fail.\n";
        exit(0);
    }

    check(::bind(msockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)), "server: can't bind local address");
    //add "::" to keep socket's bind() from messing with std's bind()

    //use listen() to open the connection window
    listen(msockfd, 5);
    //prevent zombie occuring
    signal(SIGCHLD, SIG_IGN);

    return msockfd;
}

int accept_new_connection(int msockfd){
    int ssockfd, sock_status;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    //initial activeFds & nfds to let different connection can connect at the same time
    fd_set readFds, activeFds;
    int nfds;

    // msock = passiveTCP(service, qlen); //service: service_name or port_number
    nfds = getdtablesize();
    FD_ZERO(&activeFds);
    FD_SET(msockfd, &activeFds);

    while(true){
        memcpy(&readFds, &activeFds, sizeof(readFds)); //then msockfd should be active in readFds (type:fd_set)

        //stop at select() util server get listen-msg from a client.
        check(select(nfds, &readFds, (fd_set *)0, (fd_set *)0,(struct timeval *)0), "select error");
        //// select() parameter: (nfd, read, write, session, timeout)


        if(FD_ISSET(msockfd, &readFds)){
            //msockfd
            clilen = sizeof(cli_addr);
            check(ssockfd = accept(msockfd, (struct sockaddr *) &cli_addr, &clilen), "accept error");

            FD_SET(ssockfd, &activeFds);
            initial_client_data(ssockfd, cli_addr);
        }
        for(int fd=0; fd<nfds; fd++){
            if(FD_ISSET(fd, &readFds) && fd!=msockfd){

                //ssockfd
                int handling_ssockfd = fd;
                int userID = ssockFd_userId(handling_ssockfd, GET_USERID);
                int idx = userID-1;

                int ndata;
                char buf[BUF_SIZE];

                if((ndata = read(fd, buf, BUF_SIZE))>0){
                //handle data coming from client
                    process(INITIAL_ENV, (vector<string>)0, handling_ssockfd);

                    buf[ndata-1] = '\0';
                    string line = buf;

                    stringstream input(line);
                    string tmp;
                    vector<string> items;
                    while(input>>tmp){
                        items.push_back(tmp);
                    }
                    // cout<<"setCmdsI"<<ssockfd<<end;
                    vector<COMMAND> Cmds = setCmdsInOneLine(handling_ssockfd, items); // read from socket(ssockfd)

                    shell(handling_ssockfd, Cmds); //commands in one line

                    if(!Clients[idx].exit_status){
                        check(write(handling_ssockfd, "% ", 2), "write error");
                    }
                    else{
                    ////Clients leave (user key: exit)

                        string msg = "*** User '"+Clients[idx].userName+"' left. ***\n";
                        eraseUserPipe(userPipeTable, userID, userID, CAUSE_EXIST);
                        erase_client_data(userID);
                        close(handling_ssockfd);
                        FD_CLR(handling_ssockfd, &activeFds);

                        broadcast(msg);

                    }

                }

                else{
                // error occur
                    check(ndata,"read error(msock read ssock)");
                    if(ndata==0){
                        cout << "client " << fd << " hung up." << endl;
                        //need close this ssockfd
                    }

                    broadcast("*** User '"+Clients[idx].userName+"' left. ***\n");
                    erase_client_data(userID);
                    close(fd);
                    FD_CLR(fd, &activeFds);

                }
            }
        }
    }
}


int shell(int ssockfd, vector<COMMAND> Cmds){
    int userID = ssockFd_userId(ssockfd, GET_USERID);

    bool userPipeOutflag = false;
    {
        waitpid(-1, &status, WNOHANG);
        pipe_end = 0;

        if (Cmds.empty()) return 0;
        if (debug_mode) printCmdsValue(Cmds);

        string ele = Cmds[0].CmdSeg[0];
        if (ele == "exit"){
            process(EXIT_PROCESS, Cmds[0].CmdSeg, ssockfd);
            return 0;
        }
        else if (ele == "printenv") process(PRINTENV_PROCESS, Cmds[0].CmdSeg, ssockfd);
        else if (ele == "setenv"  ) process(SETENV_PROCESS, Cmds[0].CmdSeg, ssockfd);
        else if (ele == "who" ) process(WHO_PROC, Cmds[0].CmdSeg, ssockfd);
        else if (ele == "tell") process(TELL_PROC, Cmds[0].CmdSeg, ssockfd);
        else if (ele == "yell") process(YELL_PROC, Cmds[0].CmdSeg, ssockfd);
        else if (ele == "name") process(NAME_PROC, Cmds[0].CmdSeg, ssockfd);
        else{  // exec cmds: cat, ls, number, removetag, removetag0

            for (int idx = 0; idx<Cmds.size(); idx++){ //handle one Cmd Segment at once
                if(debug_mode) cerr << "idx=" << idx << ", cnt_CmdSeg=" << Cmds.size() << endl;
                if (idx==Cmds.size()-1){
                    pipe_end = 1;
                }

                if(idx==0 && Cmds[idx].isUserPipe_i)   broadcast(Cmds[idx].userPipeI_sucMsg);
                if(pipe_end && Cmds[idx].isUserPipe_o) {
                    broadcast(Cmds[idx].userPipeO_sucMsg);
                    userPipeOutflag = true;
                }


                int ret = makePipe(Cmds[idx], Clients[userID-1].pipeTable); //return value = 0 (no produced pipe) or 1 (produced pipe)
                if(debug_mode) cerr << "makePipe done = " << ret << ", pipeTable.size()=" << Clients[userID-1].pipeTable.size() << endl;

                if (Cmds[idx].is_redirFileName==false){
                    setCmdFd(ret, Cmds[idx], Clients[userID-1].pipeTable); //set Cmd.readFd, Cmd.writeFd, Cmd.errorFd,
                    if(debug_mode) cerr << "setCmdFd done" << endl;

                    goProcessWithFork(Cmds[idx], Clients[userID-1].pipeTable, userPipeTable, ssockfd); //dup Pipe
                    if(debug_mode) cerr << "goProcessWithFork done" << endl;

                }

                ////close all fd in parent and erase specific userPipeinfo in userPipeTable (vector)
                ////so socket can close
                // if(idx==0 && Cmds[idx].isUserPipe_i) eraseUserPipe(userPipeTable, , Cmds[idx].readFd);
                if(idx==0 && Cmds[idx].isUserPipe_i) eraseUserPipe(userPipeTable, Cmds[idx].userPipeI_senderID, userID, CAUSE_USED);

                ////waitpid
                for(int i=0;i<100;i++) waitpid(-1, &status, WNOHANG);

                ////close fd in parent and erase pipeinfo in pipeTable (vector)
                erasePipes(ERASE_CMD_FOR_CMDSEG, Clients[userID-1].pipeTable, Cmds[idx]);

                if(debug_mode) cerr << "erasePipe done, pipeTable.size()="<< Clients[userID-1].pipeTable.size() << endl;

                if(debug_mode) printPipeTable(Clients[userID-1].pipeTable);
                if(debug_mode) cerr<< endl;
            }

        }
        erasePipes(ERASE_CMD_WHILE_LINE, Clients[userID-1].pipeTable, Cmds[0]);

        //集體收屍
        for (vector<pid_t>::iterator itr=Clients[userID-1].pidList.begin(); itr!=Clients[userID-1].pidList.end(); ){
            if (userPipeOutflag){
                waitpid(-1, &status, WNOHANG);
            }
            else if (Cmds.back().pipeType == NOPIPE){
                waitpid((*itr), &status, 0);
            }
            else {
                waitpid(-1, &status, WNOHANG);
            }
            itr = Clients[userID-1].pidList.erase(itr);
        }

        subPipeCrossLine(Clients[userID-1].pipeTable);
        Cmds.clear();
    }
    return 0;
}


int initial_client_data(int ssockfd, struct sockaddr_in cli_addr){
    int idx = -1;
    for(int i=0; i<30; i++){
        if(Clients[i].ssockfd == -1){
            idx = i;
            break;
        }
    }

    Clients[idx].ssockfd = ssockfd;
    Clients[idx].userID = idx+1;
    Clients[idx].userName = "(no name)";

    string clientIp = inet_ntoa(cli_addr.sin_addr);
    clientIp += ":" + to_string(ntohs(cli_addr.sin_port));
    Clients[idx].IPnPort = clientIp;

    ENV env;
    env.name = "PATH";
    env.value = "bin:.";
    Clients[idx].envTable.push_back(env);

    Clients[idx].exit_status = false;
    writeWelcomeMsg(Clients[idx].ssockfd);
    string msg = "*** User '(no name)' entered from " + Clients[idx].IPnPort + ". ***\n";
    broadcast(msg);
    check(write(Clients[idx].ssockfd, "% ", 2), "first write % error");
    return 0;
}

int isUserPipeRequest(int ssockfd, vector<string> &items,
                      bool *isUserPipe_i, bool *isUserPipe_o,
                      int *userPipeI_userID, int *userPipeO_userID,
                      string *userPipeI_sucMsg, string *userPipeO_sucMsg,
                      bool *isSenderNotExist, bool *isRecverNotExist ){


    int userID = ssockFd_userId(ssockfd, GET_USERID);
    int idx = userID-1;

    vector<string>::iterator itr = items.begin();
    while(itr!=items.end()){

        string str= *itr;
        //"<":60; ">":62
        if((str[0]==60 || str[0]==62) && str.size()!=1){

            int targetID = 0;
            string target;
            target.assign(str, 1, str.size()-1);
            stringstream geek(target);
            geek >> targetID;

            ////  userpipe Out ">"
            if(str[0]==62){

                if( !isUserExist(targetID) ){ //if user isn't exist, return err
                    string err_msg = "*** Error: user #"+to_string(targetID)+" does not exist yet. ***\n";
                    check(write(ssockfd, err_msg.c_str(), err_msg.size()), "write userpipe errmsg error");
                    *isRecverNotExist = true;
                    // return -1;
                }

                else if( isPipeExist(userID, targetID) ){ //if pipe already exist, return err
                    string err_msg = "*** Error: the pipe #"+to_string(userID)+"->#"+to_string(targetID)+" already exists. ***\n";
                    check(write(ssockfd, err_msg.c_str(), err_msg.size()), "write userpipe errmsg error");
                    *isRecverNotExist = true;
                    // return -1;
                }

                else{
                    makeUserPipe(userID, targetID);

                    *isUserPipe_o = true;
                    *userPipeO_userID = targetID;

                    string senderName = Clients[userID-1].userName;
                    string senderID   = to_string(userID);
                    string recverName = Clients[targetID-1].userName;
                    string recverID   = to_string(targetID);
                    string command = items[0];
                    for(int i=1; i<items.size(); i++){
                        command += (" "+items[i]);
                    }
                    *userPipeO_sucMsg = "*** "+senderName+" (#"+senderID+") just piped '"+command+"' to "+recverName+" (#"+recverID+") ***\n";
                }

            }


            ////  userpipe In "<"
            if(str[0]==60){
                if( !isUserExist(targetID) ){ //if user isn't exist, return err
                    string err_msg = "*** Error: user #"+to_string(targetID)+" does not exist yet. ***\n";
                    check(write(ssockfd, err_msg.c_str(), err_msg.size()), "write userpipe errmsg error");
                    *isSenderNotExist = true;
                    // return -1;
                }

                else if( !isPipeExist(targetID, userID) ){ //if pipe not exist, return err
                    string err_msg = "*** Error: the pipe #"+to_string(targetID)+"->#"+to_string(userID)+" does not exist yet. ***\n";
                    check(write(ssockfd, err_msg.c_str(), err_msg.size()), "write userpipe errmsg error");
                    *isSenderNotExist = true;
                    // return -1;
                }

                else{
                    *isUserPipe_i = true;
                    *userPipeI_userID = targetID;

                    string senderName = Clients[targetID-1].userName;
                    string senderID   = to_string(targetID);
                    string recverName = Clients[userID-1].userName;
                    string recverID   = to_string(userID);
                    string command = items[0];
                    for(int i=1; i<items.size(); i++){
                        command += (" "+items[i]);
                    }
                    *userPipeI_sucMsg = "*** "+recverName+" (#"+recverID+") just received from "+senderName+" (#"+senderID+") by '"+command+"' ***\n";
                }

            }
        }
        itr++;
    }

    itr = items.begin();
    while(itr!=items.end()){
        string str= *itr;
        //"<":60; ">":62
        if((str[0]==60 || str[0]==62) && str.size()!=1)
            itr = items.erase(itr);
        else
            itr++;
    }
    return 0;
}

vector<COMMAND> setCmdsInOneLine(int ssockfd, vector<string> items){
    vector<COMMAND> Cmds;

    if (items.empty()) return (vector<COMMAND>)0; //vector items is empty

    //////////   functions between clients   ////////////////
    string cmd0 = items[0];

    COMMAND Ucmd;
    Ucmd.writeFd = ssockfd;
    Ucmd.errorFd = ssockfd;
    if(cmd0=="who"){
        Ucmd.argc = 1;
        Ucmd.CmdSeg.push_back(cmd0);

        Cmds.push_back(Ucmd);
        return Cmds;
    }
    else if(cmd0=="tell"){
        Ucmd.argc = 3;
        Ucmd.CmdSeg.push_back(cmd0);     //argv[0]
        Ucmd.CmdSeg.push_back(items[1]); //argv[1]

        string content = items[2];
        for(int i=3; i<items.size(); i++){
            content += (" "+items[i]);
        }
        Ucmd.CmdSeg.push_back(content);  //argv[2]

        Cmds.push_back(Ucmd);
        return Cmds;
    }
    else if(cmd0=="yell"){
        Ucmd.argc = 2;
        Ucmd.CmdSeg.push_back(cmd0);     //argv[0]

        string content = items[1];
        for(int i=2; i<items.size(); i++){
            content += (" "+items[i]);
        }
        Ucmd.CmdSeg.push_back(content);  //argv[1]

        Cmds.push_back(Ucmd);
        return Cmds;
    }
    else if(cmd0=="name"){
        Ucmd.argc = 2;
        Ucmd.CmdSeg.push_back(cmd0);     //argv[0]
        Ucmd.CmdSeg.push_back(items[1]); //argv[1]

        Cmds.push_back(Ucmd);
        return Cmds;
    }


    bool isUserPipe_i = false; //out to other user
    bool isUserPipe_o = false; //in from other user
    int  userPipeI_ID = 0;
    int  userPipeO_ID = 0;
    string userPipeI_sucMsg;   //if userPipe in success, the success msg
    string userPipeO_sucMsg;   //if userPipe out success, the success msg
    bool isSenderNotExist = false;
    bool isRecverNotExist = false;
    isUserPipeRequest(ssockfd, items, &isUserPipe_i, &isUserPipe_o, &userPipeI_ID, &userPipeO_ID,
                        &userPipeI_sucMsg, &userPipeO_sucMsg, &isSenderNotExist, &isRecverNotExist);

    //////////   functions in single client   ////////////////
    int cnt_CmdSeg = cntCmdSeg(items);
    int *pipeType = getPipeType(items);
    int p[cnt_CmdSeg][2];
    int is_redirCmd = 0;
    int is_redirCmdIdx = -1;

    for (int idx = 0; idx<cnt_CmdSeg; idx++){
        COMMAND cmd;
        cmd.CmdSeg = getCmdSeg(idx, cnt_CmdSeg, items);
        cmd.argc = cmd.CmdSeg.size();
        cmd.pipeType = pipeType[idx];
        if (cmd.pipeType==REDIRPIPE){
            is_redirCmd = 1;
            is_redirCmdIdx = idx;
            cmd.redirFileName = getCmdSeg(idx+1, cnt_CmdSeg, items);
        }
        if (is_redirCmd == 1 && idx-1==is_redirCmdIdx){
            cmd.is_redirFileName = true;
        }

        cmd.cmdPos = (idx==0)? CmdAtBegin:((idx==cnt_CmdSeg-1)? CmdAtEnd:CmdInMid);
        //readFd, writeFd, is_stdin_from_prev_line

        cmd.is_stdout_cross_line = ((cmd.pipeType==NUMPIPE)? true : false);
        if (cmd.is_stdout_cross_line){
            string str1 = items[items.size()-1];
            string str2;
            str2.assign(str1, 1, str1.size()-1);
            stringstream geek(str2);
            int numline = 0;
            geek >> numline;
            cmd.cnt_cross_line = numline;
        }
        cmd.is_stderr_cross_line = ((cmd.pipeType==IN2PIPE)? true : false);
        if (cmd.is_stderr_cross_line){
            string str1 = items[items.size()-1];
            string str2;
            str2.assign(str1, 1, str1.size()-1);
            stringstream geek(str2);
            int numline = 0;
            geek >> numline;
            cmd.cnt_cross_line = numline;
        }

        cmd.writeFd = ssockfd;
        cmd.errorFd = ssockfd;

        int userID = ssockFd_userId(ssockfd, GET_USERID);
        if(isUserPipe_i){
            cmd.isUserPipe_i = true;
            cmd.userPipeI_senderID = userPipeI_ID;
            cmd.userPipeI_sucMsg = userPipeI_sucMsg;

            int outPipefd = seekUserPipe(userPipeI_ID, userID, GET_OUTPIPE);
            cmd.readFd = outPipefd;
            cout << "isUserPipe_i, cmd.readFd="<<cmd.readFd<<endl;
        }
        if(isUserPipe_o){
            cmd.isUserPipe_o = true;
            cmd.userPipeO_recverID = userPipeO_ID;
            cmd.userPipeO_sucMsg = userPipeO_sucMsg;

            int enterPipefd = seekUserPipe(userID, userPipeO_ID, GET_ENTERPIPE);
            cmd.writeFd = enterPipefd;
            cmd.errorFd = enterPipefd;
            cout << "isUserPipe_o, idx="<< idx<<", cmd.writeFd="<<cmd.writeFd<<endl;
        }

        if(isSenderNotExist){
            cmd.readFd = devNull_fd;
        }
        if(isRecverNotExist){
            cmd.writeFd = devNull_fd;
        }

        Cmds.push_back(cmd);
    }
    return Cmds;
}

int process(int processType, vector<string> cmd, int ssockfd){ //argv[2](sockfd) is for printenv

    switch (processType) {
        case INITIAL_ENV:
            {
                int userID = ssockFd_userId(ssockfd, GET_USERID);
                int idx = userID-1;
                for(int i=0; i<Clients[idx].envTable.size(); i++){
                    check(setenv(Clients[idx].envTable[i].name.c_str(), Clients[idx].envTable[i].value.c_str(),1), "initial env error");
                }
            }
            break;
        case BIN_PROCESS:
            {
                if(debug_mode) cerr << "bin_process" << endl;

                int argc = cmd.size(); //ex. 3
                char *argv[argc+1]; // argv[4] = ["cat", "t1.txt", "t2.txt", NULL]
                argv[argc] = NULL;
                for (int i=0; i<argc; i++){
                    string str = cmd[i];
                    argv[i] = (char*)malloc(str.size()+1);
                    strcpy(argv[i], str.c_str());
                }

                string str = cmd[0];
                // setenv("PATH", "bin:.", 1);
                if(execvp(str.c_str(), argv)==-1){
                    cerr << "Unknown command: [" << str << "]." << endl;
                }
            }
            break;
        case SETENV_PROCESS:
            { //argc=3, setenv name value
                string name = cmd[1];
                string value = cmd[2];
                char *argv1 = new char[name.size()+1];
                char *argv2 = new char[value.size()+1];
                strcpy(argv1, name.c_str());
                strcpy(argv2, value.c_str());
                setenv(argv1,argv2, 1);

                //store in client's data
                int meSsockfd = ssockfd;
                int meUserID = ssockFd_userId(meSsockfd, GET_USERID);
                int meIdx = meUserID-1;

                //// make sure that the "name" in cmd exists in client's env data
                int nEnv = Clients[meIdx].envTable.size();
                for(int i=0; i<nEnv; i++){
                    if(name == Clients[meIdx].envTable[i].name){
                        Clients[meIdx].envTable[i].value = value;
                        return 0;
                    }
                }
                //// the "name" in cmd doesn't exist in client's env data
                ENV env;
                env.name = name;
                env.value = value;
                Clients[meIdx].envTable.push_back(env);
            }
            break;
        case PRINTENV_PROCESS:
            { //argc=2, printenv name
                string str = cmd[1];

                char *argv = new char[str.size()+1];
                strcpy(argv, str.c_str());

                string ret = getenv(argv);
                ret += "\n";
                check(write(ssockfd, ret.c_str(), ret.size()), "write error");
            }
            break;
        case EXIT_PROCESS:
            {
                int userID = ssockFd_userId(ssockfd, GET_USERID);
                Clients[userID-1].exit_status = true;
            }
            break;
        case WHO_PROC:
            { //argc=1, who
                int meSsockfd = ssockfd;
                int meUserID = ssockFd_userId(meSsockfd, GET_USERID);
                int meIdx = meUserID-1;

                string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
                check(write(meSsockfd, msg.c_str(), msg.size()), "write error");

                for(int i=0; i<30; i++){
                    if(Clients[i].ssockfd>0){

                        string msg = to_string(i+1) +"\t"+ Clients[i].userName +"\t"+ Clients[i].IPnPort;
                        if(i==meIdx) msg+=("\t<-me");
                        msg+="\n";

                        check(write(meSsockfd, msg.c_str(), msg.size()), "write error");
                    }
                }
            }
            break;
        case TELL_PROC:
            { //argc=3, tell <id> <msg>
                int meSsockfd = ssockfd;
                int meUserID = ssockFd_userId(meSsockfd, GET_USERID);
                int meIdx = meUserID-1;

                int targetID = atoi(cmd[1].c_str());
                int targetIdx = targetID-1;
                int targetSsockfd = Clients[targetIdx].ssockfd;


                if(targetSsockfd==-1){
                    string msg = "*** Error: user #" + to_string(targetID) + " does not exist yet. ***\n";
                    check(write(meSsockfd, msg.c_str(), msg.size()), "write error");
                }
                else{
                    string msg = "*** "+Clients[meIdx].userName+" told you ***: "+cmd[2]+"\n";
                    check(write(targetSsockfd, msg.c_str(), msg.size()), "write error");
                }

            }
            break;
        case YELL_PROC:
            { //argc=2, yell <msg>
                string msg = cmd[1];
                int meSsockfd = ssockfd;
                int meUserID = ssockFd_userId(meSsockfd, GET_USERID);
                int meIdx = meUserID-1;

                broadcast("*** "+Clients[meIdx].userName+" yelled ***: "+msg+"\n");
            }
            break;
        case NAME_PROC:
            { //argc=2, name <new name>
                string newName = cmd[1];
                int meSsockfd = ssockfd;
                int meUserID = ssockFd_userId(meSsockfd, GET_USERID);
                int meIdx = meUserID-1;

                for(int i=0; i<30; i++){
                    if(Clients[i].ssockfd==-1) continue;
                    if(Clients[i].userName == newName){
                        string msg = "*** User '"+newName+"' already exists. ***\n";
                        check(write(meSsockfd, msg.c_str(), msg.size()), "write error");
                        return -1;
                    }
                }

                Clients[meIdx].userName = newName;
                broadcast("*** User from "+Clients[meIdx].IPnPort+" is named '"+Clients[meIdx].userName+"'. ***\n");
            }
            break;
        default:
            cerr << "ProcessType value err." << endl;
            break;
    }
    return 0;
}

//Need make pipe or not
int makePipe(COMMAND Cmd, vector<PIPE> &pipeTable){

    if (Cmd.pipeType==NOPIPE){
        return 0;
    }
    else if (Cmd.pipeType==REDIRPIPE){

        FILE *wfile;
        wfile = fopen(Cmd.redirFileName[0].c_str(), "w");
        if(wfile == NULL) perror("Open file error!");
        int p1 = fileno(wfile);

        PIPE uniPipe;
        uniPipe.enterPipe = p1;
        uniPipe.outPipe = STDOUT_FILENO;
        uniPipe.is_redirPipe = true;
        pipeTable.push_back(uniPipe);
        if(debug_mode) cerr << "  build redirPipe, p1= "<< p1 << endl;
        return 2;
    }
    else if(Cmd.pipeType==NUMPIPE || Cmd.pipeType==IN2PIPE){
        // when cntLine same, use same pipe
        bool need_build_pipe_flag = true;
        vector<PIPE>::iterator itr = pipeTable.begin();

        while(itr != pipeTable.end()){
            if((*itr).cntLine==Cmd.cnt_cross_line){
                need_build_pipe_flag = false;
            }
            itr++;
        }
        if (need_build_pipe_flag){
            PIPE uniPipe;
            int p[2];
            if (pipe(p)<0){
                if(debug_mode) cerr << "pipe error" << endl;
            }
            else {
                uniPipe.enterPipe = p[1];
                uniPipe.outPipe = p[0];
                uniPipe.cntLine = Cmd.cnt_cross_line;
                pipeTable.push_back(uniPipe);
                if(debug_mode) cerr << "  build pipe, p= "<< p[0] << "/"<< p[1] << endl;
            }
            return 1;
        }
        else{
            return 0;
        }
    }
    else{
        PIPE uniPipe;
        int p[2];
        if (pipe(p)<0){
            if(debug_mode) cerr << "pipe error" << endl;
        }
        else {
            uniPipe.enterPipe = p[1];
            uniPipe.outPipe = p[0];
            uniPipe.cntLine = Cmd.cnt_cross_line;
            pipeTable.push_back(uniPipe);
            if(debug_mode) cerr << "  build pipe, p= "<< p[0] << "/"<< p[1] << endl;
        }
        return 1;
    }
}

bool isStdinFromPrevLine(vector<PIPE> &pipeTable, int pipeType){
    vector<PIPE>::iterator itr = pipeTable.begin();
    if (pipeTable.size()==0){
        return false;
    }
    else if(pipeType==NOPIPE){
        while(itr!=pipeTable.end()){
            if((*itr).cntLine){
                itr++;
            }
            else{
                return true;
            }
        }
    }
    else if(pipeType!=NOPIPE){
        while(itr!=pipeTable.end()-1){
            if((*itr).cntLine){
                itr++;
            }
            else{
                return true;
            }
        }
    }
    return false;
}

int setCmdFd(int isBuiltPipe, COMMAND &Cmd, vector<PIPE> &pipeTable){
    //set Cmd.readFd, Cmd.writeFd, Cmd.errorFd, Cmd.is_stdin_from_prev_line
    if(debug_mode) cerr << "  pos=" << Cmd.cmdPos << ", pipeType="<< Cmd.pipeType << endl;

    if (Cmd.cmdPos == CmdAtBegin){
        //if has stdin
        Cmd.is_stdin_from_prev_line = isStdinFromPrevLine(pipeTable, Cmd.pipeType);
        if (Cmd.is_stdin_from_prev_line){
            vector<PIPE>::iterator itr = pipeTable.begin();
            while(itr!=pipeTable.end()){
                if((*itr).cntLine==0){
                    Cmd.readFd = (*itr).outPipe;
                    break;
                }
                itr++;
            }
        }
        //if need pipe out
        if (Cmd.pipeType!=NOPIPE){
            vector<PIPE>::iterator itr = pipeTable.end()-1;
            Cmd.writeFd = (*itr).enterPipe;
            if (Cmd.pipeType==IN2PIPE){
                Cmd.errorFd = (*itr).enterPipe;
            }
        }
    }

    else if (Cmd.cmdPos == CmdInMid){
        //must need pipe in & pipe out

        ////pipe in
        vector<PIPE>::iterator itrI = pipeTable.end()-2;
        Cmd.readFd = (*itrI).outPipe;

        ////pipe out
        vector<PIPE>::iterator itrO = pipeTable.end()-1;
        Cmd.writeFd = (*itrO).enterPipe;
    }

    else if (Cmd.cmdPos == CmdAtEnd && Cmd.is_redirFileName==false){
        //pipe in //may pipe out
        if (Cmd.pipeType==NOPIPE){
            vector<PIPE>::iterator itrI = pipeTable.end()-1;
            if (pipeTable.size()==1){
                itrI = pipeTable.begin();
            }
            Cmd.readFd = (*itrI).outPipe;
        }
        // NUMPIPE || IN2PIPEs
        else if (Cmd.pipeType!=NOPIPE) {
            //if NUMPIPE || IN2PIPE 使用新創的PIPE
            if (isBuiltPipe){
                vector<PIPE>::iterator itrI = pipeTable.end()-2;
                Cmd.readFd = (*itrI).outPipe;

                vector<PIPE>::iterator itrO = pipeTable.end()-1;
                Cmd.writeFd = (*itrO).enterPipe;
                if (Cmd.pipeType==IN2PIPE){
                    Cmd.errorFd = (*itrO).enterPipe;
                }
            }
            //else NUMPIPE || IN2PIPE 使用之前創建的PIPE
            else {

                vector<PIPE>::iterator itrI = pipeTable.end()-1;
                Cmd.readFd = (*itrI).outPipe;

                vector<PIPE>::iterator itrO = pipeTable.begin();
                while(itrO != pipeTable.end()){
                    if((*itrO).cntLine==Cmd.cnt_cross_line){
                        Cmd.writeFd = (*itrO).enterPipe;
                        if (Cmd.pipeType==IN2PIPE){
                            Cmd.errorFd = (*itrO).enterPipe;
                        }
                        break;
                    }
                    itrO++;
                }
            }
        }
    }
    if(debug_mode) cerr << "  setCmd: Cmd.readFd=" << Cmd.readFd << ", Cmd.writeFd=" << Cmd.writeFd << ", Cmd.errorFd=" << Cmd.errorFd << endl;
    return 0;
}

int goProcessWithFork(COMMAND &Cmd, vector<PIPE> &pipeTable, vector<USERPIPE> &userPipeTable, int ssockfd){

    int rFd = Cmd.readFd;
    int wFd = Cmd.writeFd;
    int eFd = Cmd.errorFd;
    int cmdPos = Cmd.cmdPos;
    int usingPipeType = Cmd.pipeType;

    bool is_stdin = Cmd.is_stdin_from_prev_line;

    vector<string> CmdSeg = Cmd.CmdSeg;

    int userID = ssockFd_userId(ssockfd, GET_USERID);

    pid_t pid;
    while((pid = fork()) < 0){
        while((waitpid(-1, &status, WNOHANG)) > 0){

        }
    }
    Clients[userID-1].pidList.push_back(pid);

    if (pid==0){
        //child

        dup2(rFd, STDIN_FILENO);
        dup2(wFd, STDOUT_FILENO);
        dup2(eFd, STDERR_FILENO);

        //close rFd & wFd on child fork FD_table
        if (rFd!=0){
            close(rFd);
        }
        if (wFd!=1){
            close(wFd);
        }
        if (eFd!=2){
            close(eFd);
        }
        // printUserPipeTable(userPipeTable);
        closeAllPipes();
        closeAllUserPipes(userPipeTable);

        if (Cmd.is_redirFileName == false){
            process(BIN_PROCESS, CmdSeg, 0);
        }

        exit(-1);
    }
    else if (pid>0){
        //parent
    }
    else {
        cerr << "fork error" << endl;
    }
    return 0;
}

int closeAllPipes(){
    // close all pipes in fork child
    for(int idx = 0; idx<30; idx++){
        if(Clients[idx].ssockfd>0){

            vector<PIPE>::iterator itr = Clients[idx].pipeTable.begin();
            if (Clients[idx].pipeTable.empty()){ //ex. "ls" | cat
                continue;
            }

            while(itr != Clients[idx].pipeTable.end()){
                if ((*itr).is_redirPipe == false){
                    close((*itr).enterPipe);
                    close((*itr).outPipe);
                }
                itr++;
            }

        }

    }

    return 0;
}

int erasePipes(int eraseType, vector<PIPE> &pipeTable, COMMAND cmd){
    switch (eraseType) {
    case ERASE_CMD_FOR_CMDSEG:
        {
            vector<PIPE>::iterator itr = pipeTable.begin();
            for (vector<PIPE>::iterator itr = pipeTable.begin(); itr!=pipeTable.end();){
                if((*itr).cntLine){
                    itr++;
                }
                else{
                    if (cmd.writeFd == (*itr).enterPipe){ // last one
                        if(debug_mode) cerr << "correct" << endl;
                        return 0;
                    }
                    else {
                        if ((*itr).is_redirPipe==true){
                            itr = pipeTable.erase(itr);
                        }
                        else{
                            close((*itr).enterPipe);
                            close((*itr).outPipe);
                            itr = pipeTable.erase(itr);
                        }
                    }
                }
            }
            return -1;
        }
        break;
    case ERASE_CMD_WHILE_LINE:
        {
            vector<PIPE>::iterator itr = pipeTable.begin();
            while(itr != pipeTable.end()){
                if((*itr).cntLine){
                    itr++;
                }
                else{
                    if ((*itr).is_redirPipe==true){
                        itr = pipeTable.erase(itr);
                    }
                    else{
                        close((*itr).enterPipe);
                        close((*itr).outPipe);
                        itr = pipeTable.erase(itr);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

int subPipeCrossLine(vector<PIPE> &pipeTable){
    if (pipeTable.size()==0){
        return 0;
    }
    vector<PIPE>::iterator itr = pipeTable.begin();
    while(itr != pipeTable.end()){
        if((*itr).cntLine){
            (*itr).cntLine--;
            // itr++;
        }
        else{
            if(debug_mode) cerr << "useless pipe not clean yet!" << endl;
        }
        itr++;
    }
    return 0;
}


bool isUserExist(int userID){
    bool ret = (Clients[userID-1].ssockfd>0)? true : false;
    return ret;
}

bool isPipeExist(int senderID, int recverID){
    if(userPipeTable.empty()){
        return false;
    }
    for(int i=0; i<userPipeTable.size(); i++){
        if(userPipeTable[i].senderUserID==senderID && userPipeTable[i].recverUserID==recverID){
            return true;
        }
    }
    return false;
}

int makeUserPipe(int senderID, int recverID){
    USERPIPE userpipe;
    userpipe.senderUserID = senderID;
    userpipe.recverUserID = recverID;
    int fd[2];
    check(pipe(fd), "userpipe error");
    userpipe.enterPipe = fd[1];
    userpipe.outPipe   = fd[0];

    userPipeTable.push_back(userpipe);

    return 0;
}

int seekUserPipe(int senderID, int recverID, int type){
    if(userPipeTable.empty()){
        return -1;
    }
    switch (type){
        case GET_ENTERPIPE:
            {
                for(int i=0; i<userPipeTable.size(); i++){
                    if(userPipeTable[i].senderUserID==senderID
                       && userPipeTable[i].recverUserID==recverID){
                        return userPipeTable[i].enterPipe;
                    }
                }
            }
            break;
        case GET_OUTPIPE:
            {
                for(int i=0; i<userPipeTable.size(); i++){
                    if(userPipeTable[i].senderUserID==senderID
                       && userPipeTable[i].recverUserID==recverID){
                        return userPipeTable[i].outPipe;
                    }
                }
            }
            break;
        default:
            break;
    }
    return -1;
}

int erase_client_data(int userID){
    int idx = userID-1;
    Clients[idx].ssockfd = -1;
    Clients[idx].userID  = -1;
    Clients[idx].userName = "";
    Clients[idx].IPnPort  = "";
    Clients[idx].envTable.clear();

    Clients[idx].exit_status = false;

    Clients[idx].pipeTable.clear();
    Clients[idx].pidList.clear();

    return 0;
}

int eraseUserPipe(vector<USERPIPE> &userPipeTable, int senderID, int recverID, int type){

    // int outPipefd = ssockFd_userId(recverID, GET_SOCKFD);

    if (userPipeTable.empty()){
        return 1;
    }

    vector<USERPIPE>::iterator itr = userPipeTable.begin();
    while(itr != userPipeTable.end()){
        // if((*itr).outPipe == outPipefd){
        //     close((*itr).enterPipe);
        //     close((*itr).outPipe);
        // }

        if(type == CAUSE_USED){
            if((*itr).senderUserID==senderID && (*itr).recverUserID==recverID){
                close((*itr).enterPipe);
                close((*itr).outPipe);
                itr = userPipeTable.erase(itr);
            }
            else itr++;
        }
        if(type == CAUSE_EXIST){
            if((*itr).senderUserID==senderID || (*itr).recverUserID==recverID){
                close((*itr).enterPipe);
                close((*itr).outPipe);
                itr = userPipeTable.erase(itr);
            }
            else itr++;
        }

    }

    return 0;
}

int closeAllUserPipes(vector<USERPIPE> userPipeTable){
    // close user pipes in fork child
    if (userPipeTable.empty()){
        return 1;
    }
    vector<USERPIPE>::iterator itr = userPipeTable.begin();
    while(itr != userPipeTable.end()){
        close((*itr).enterPipe);
        close((*itr).outPipe);

        itr++;
    }
    return 0;
}

int ssockFd_userId(int src, int type){
    switch (type){
        case GET_USERID:
            {
                for(int i=0; i<30; i++){
                    if(Clients[i].ssockfd == src){
                        return Clients[i].userID;
                    }
                }
                return -1;
            }
            break;
        case GET_SOCKFD:
            {
                for(int i=0; i<30; i++){
                    if(Clients[i].userID == src){
                        return Clients[i].ssockfd;
                    }
                }
                return -1;
            }
            break;
        default:
            return -1;
            break;
    }
}

int writeWelcomeMsg(int ssockfd){
    string str = "****************************************\n";
    str += "** Welcome to the information server. **\n";
    str += "****************************************\n";

    check(write(ssockfd, str.c_str(), str.size()), "write error");
    return 0;
}

int broadcast(string msg){
    for(int i=0; i<30; i++){
        if(Clients[i].ssockfd>0){
            check(write(Clients[i].ssockfd, msg.c_str(), msg.size()), "broadcast error");
        }
    }
    return 0;
}

bool check(int exp, string err_msg){
    if(exp < 0) {
        cerr << err_msg << endl;
        return false;
    }
    return true;
}

int printClients(){
    for(int i=0; i<30; i++){
        if(Clients[i].ssockfd>0)
            cout <<"Client "<< i << ":  ssockfd=" << Clients[i].ssockfd << endl;
    }
    return 0;
}

int printUserPipeTable(vector<USERPIPE> &userPipeTable){
    if (userPipeTable.empty()){
        return 0;
    }
    cerr<<"userPipeTable.size()="<<userPipeTable.size()<<endl;
    vector<USERPIPE>::iterator itr = userPipeTable.begin();
    while(itr != userPipeTable.end()){
        cerr << "enterPipe="<<(*itr).enterPipe << ", outPipe="<<(*itr).outPipe << ", senderID="<<(*itr).senderUserID << ", recverID="<<(*itr).recverUserID << endl;
        itr++;
    }
    return 0;
}

int printCmdsValue(vector<COMMAND> Cmds){
    for (int idx = 0; idx<Cmds.size(); idx++){
        cerr << "idx=" << idx << endl;
        cerr << "CmdSeg[0]=" << Cmds[idx].CmdSeg[0] << endl ;
        cerr << "argc=" << Cmds[idx].argc << endl;
        cerr << "pipeType=" << Cmds[idx].pipeType << endl;
        cerr << "cmdPos=" << Cmds[idx].cmdPos << endl;
        cerr << "is_redirFileName=" << Cmds[idx].is_redirFileName << endl;
        if (Cmds[idx].pipeType == REDIRPIPE){
            cerr << "redirFileName[0]" << Cmds[idx].redirFileName[0] << endl;
        }
        cerr << endl;
    }
    return 0;
}

int printPipeTable(vector<PIPE> &pipeTable){
    vector<PIPE>::iterator itr = pipeTable.begin();
    if (pipeTable.empty()){
        return 0;
    }
    while(itr != pipeTable.end()){
        if(debug_mode) cerr << "enterPipe="<<(*itr).enterPipe << ", outPipe="<<(*itr).outPipe << ", cntLine="<<(*itr).cntLine << ", is_redirPipe="<<(*itr).is_redirPipe << endl;
        itr++;
    }
    return 0;
}


int cntCmdSeg(vector<string> vec){
    int cnt_CmdSeg = 1;
    for (int i=0; i<vec.size(); i++){
        string ele = vec[i];
        if ((int(ele[0])==124  && i!=vec.size()-1) || int(ele[0])== 62) {
            // "|":124, ">":62
            cnt_CmdSeg++;
        }
    }
    return cnt_CmdSeg;
}

int* getPipeType(vector<string> vec){

    int cnt_CmdSeg = 1;
    for (int i=0; i<vec.size(); i++){
        string ele = vec[i];
        if ((int(ele[0])==124  && i!=vec.size()-1) || int(ele[0])== 62) {
            // "|":124, ">":62
            cnt_CmdSeg++;
        }
    }

    int* pipeType = new int[cnt_CmdSeg];
    int cnt = 0;
    pipeType[0] = NOPIPE; //one command in a line
    pipeType[cnt_CmdSeg-1] = NOPIPE; //suppose last cmd in line no need cross line pipe

    for(int i = 0; i < vec.size(); i++) {
        string ele = vec[i]; // "|":124, "!":33, ">":62
        if (i==vec.size()-1 && int(ele[0])==124){
            //number pipe, ex. ls |N
            pipeType[cnt] = NUMPIPE;
            cnt++;
        }
        else if (int(ele[0])== 33){
            //stdout+stderr pipe, ex. ls !N
            pipeType[cnt] = IN2PIPE;
            cnt++;
        }
        else if (i!=vec.size()-1 && int(ele[0])==124) {
            //normal pipe, ex. ls | cat
            pipeType[cnt] = ORDPIPE;
            cnt++;
        }
        else if (int(ele[0])== 62){
            //redirection pipe, ex. ls > test.txt
            pipeType[cnt] = REDIRPIPE;
            cnt++;
        }
    }
    return pipeType;
}

vector<string> getCmdSeg(int idx_CmdSeg, int cnt_CmdSeg, vector<string> vec){
    int index=0;
    vector<string> vecSeg;
    for (int i=0; i<vec.size(); i++){ //run all vec items
        if (index==idx_CmdSeg){ //in the same Segment
            for (int j=i; j<vec.size(); j++){
                string ele = vec[j];
                if (int(ele[0])!=124 && int(ele[0])!= 33 && int(ele[0])!= 62) { // "|":124, "!":33, ">":62
                    vecSeg.push_back(ele);
                }
                else {
                    return vecSeg;
                }
            }
            break;
        }
        else { //find Segment
            string ele = vec[i];
            if ((int(ele[0])==124  && i!=vec.size()-1) || int(ele[0])== 62) { // "|":124, "!":33, ">":62
                index++;
            }
        }
    }
    return vecSeg;
}
