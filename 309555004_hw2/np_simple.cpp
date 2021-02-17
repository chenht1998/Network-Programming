
#include "np_simple.h"
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
#include <netinet/in.h> //bind(), AF_INET
#include<arpa/inet.h> //inet_ntop()

using namespace std;

int pipe_end = 0;
int status;

bool debug_mode = false;

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
    if (pipeTable.size()==0){
        return 0;
    }
    while(itr != pipeTable.end()){
        if(debug_mode) cerr << "enterPipe="<<(*itr).enterPipe << ", outPipe="<<(*itr).outPipe << ", cntLine="<<(*itr).cntLine << ", is_redirPipe="<<(*itr).is_redirPipe << endl;
        itr++;
    }
    return 0;
}

vector<string> getItemsInOneLine(){
    string line;
    string tmp;
    vector<string> vec;

    // int nbytes;
    // char *buf;
    // int ndata;
    // ndata = read(sockfd, buf, nbytes);

    getline(cin,line);
    // fputs(line.c_str(),hstory);
    // line = "ls";
    stringstream input(line);
    while(input>>tmp){
        vec.push_back(tmp);
    }
    return vec;
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

vector<COMMAND> setCmdsInOneLine(){
    vector<string> items = getItemsInOneLine();
    if (items.size()==0) return Cmds;
    int cnt_CmdSeg = cntCmdSeg(items);
    int *pipeType = getPipeType(items);
    // cerr << "*pipeType=" << *pipeType << ", cnt_CmdSeg="  << cnt_CmdSeg<< endl;
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
        Cmds.push_back(cmd);
    }
    return Cmds;
}

int process(int processType, vector<string> cmd){

    switch (processType) {
        case BIN_PROCESS:
            {
                if(debug_mode) cerr << "bin_process" << endl;

                int argc = cmd.size(); //ex. 3
                char *argv[argc+1]; // argv[4] = ["cat", "t1.txt", "t2.txt", NULL]
                argv[argc] = NULL;
                for (int i=0; i<argc; i++){
                    string str = cmd[i];
                    argv[i] = (char*)malloc(str.size()+1);
                    strcpy(argv[i],str.c_str());
                }

                string str = cmd[0];
                // setenv("PATH", "bin:.", 1);
                if(execvp(str.c_str(), argv)==-1){
                    cerr << "Unknown command: [" << str << "]." << endl;
                }
            }
            break;
        case SETENV_PROCESS:
            {
                string str1 = cmd[1];
                string str2 = cmd[2];
                char *argv1 = new char[str1.size()+1];
                char *argv2 = new char[str2.size()+1];
                strcpy(argv1, str1.c_str());
                strcpy(argv2, str2.c_str());
                setenv(argv1,argv2, 1);
            }
            break;
        case PRINTENV_PROCESS:
            {
                string str = cmd[1];
                // cout << str << endl;
                char *argv = new char[str.size()+1];
                strcpy(argv, str.c_str());
                cout << getenv(argv) << endl;
            }
            break;
        case EXIT_PROCESS:
            return 1;
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

int goProcessWithFork(COMMAND &Cmd, vector<PIPE> &pipeTable){

    int rFd = Cmd.readFd;
    int wFd = Cmd.writeFd;
    int eFd = Cmd.errorFd;
    int cmdPos = Cmd.cmdPos;
    int usingPipeType = Cmd.pipeType;

    bool is_stdin = Cmd.is_stdin_from_prev_line;

    vector<string> CmdSeg = Cmd.CmdSeg;

    pid_t pid;
    while((pid = fork()) < 0){
        while((waitpid(-1, &status, WNOHANG)) > 0){

        }
    }
    pidList.push_back(pid);

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
        closeAllPipes(pipeTable);
        if (Cmd.is_redirFileName == false){
            process(BIN_PROCESS, CmdSeg);
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

int closeAllPipes(vector<PIPE> pipeTable){
    // close pipes in fork child
    vector<PIPE>::iterator itr = pipeTable.begin();
    if (pipeTable.size()==0){ //ex. "ls" | cat
        return 1;
    }
    while(itr != pipeTable.end()){
        if ((*itr).is_redirPipe == false){
            close((*itr).enterPipe);
            close((*itr).outPipe);
        }
        itr++;
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

int shell(){

    if(setenv("PATH","bin:.", 1)!=0){
        if(debug_mode) cerr << "setenv err" << endl;
    }
    while(1){

        waitpid(-1, &status, WNOHANG);
        pipe_end = 0;

        cout << "% ";
        Cmds = setCmdsInOneLine();
        if (Cmds.size()==0) continue;
        if (debug_mode){
            printCmdsValue(Cmds);
        }


        for (int idx = 0; idx<Cmds.size(); idx++){ //handle one Cmd Segment at once
            if(debug_mode) cerr << "idx=" << idx << ", cnt_CmdSeg=" << Cmds.size() << endl;
            if (idx==Cmds.size()-1){
                pipe_end = 1;
            }

            string ele = Cmds[idx].CmdSeg[0];
            if (ele.compare("printenv")==0){
                process(PRINTENV_PROCESS, Cmds[idx].CmdSeg);
                continue;
            }
            else if (ele.compare("setenv")==0){
                process(SETENV_PROCESS, Cmds[idx].CmdSeg);
                continue;
            }
            else if (ele.compare("exit")==0){
                process(EXIT_PROCESS, Cmds[idx].CmdSeg);
                return CLIENT_EXIT;
            }


            int ret = makePipe(Cmds[idx], pipeTable); //return value = 0 (no produced pipe) or 1 (produced pipe)
            if(debug_mode) cerr << "makePipe done = " << ret << ", pipeTable.size()=" << pipeTable.size() << endl;

            if (Cmds[idx].is_redirFileName==false){
                setCmdFd(ret, Cmds[idx], pipeTable); //set Cmd.readFd, Cmd.writeFd, Cmd.errorFd,
                if(debug_mode) cerr << "setCmdFd done" << endl;

                goProcessWithFork(Cmds[idx], pipeTable); //dup Pipe
                if(debug_mode) cerr << "goProcessWithFork done" << endl;

            }
            ////waitpid
            for(int i=0;i<100;i++) waitpid(-1, &status, WNOHANG);

            ////close fd in parent
            erasePipes(ERASE_CMD_FOR_CMDSEG, pipeTable, Cmds[idx]);

            if(debug_mode) cerr << "erasePipe done, pipeTable.size()="<< pipeTable.size() << endl;

            if(debug_mode) printPipeTable(pipeTable);
            if(debug_mode) cerr<< endl;
        }

        erasePipes(ERASE_CMD_WHILE_LINE, pipeTable, Cmds[0]);

        //集體收屍
        for (vector<pid_t>::iterator itr=pidList.begin(); itr!=pidList.end(); ){
            if (Cmds.back().pipeType == NOPIPE){
                waitpid((*itr), &status, 0);
            }
            else {
                waitpid(-1, &status, WNOHANG);
            }
            itr = pidList.erase(itr);
        }

        subPipeCrossLine(pipeTable);
        Cmds.clear();
    }
    return 0;
}


int build_socket(int port){
    int sockfd, newsockfd, childpid, sock_status;
    socklen_t clilen;
    struct sockaddr_in cli_addr, serv_addr;

    //open socket
    if((sockfd = socket(AF_INET, SOCK_STREAM,0))<0){
        cerr << "server: can't open stream socket" << endl;
    }

    //bind local addr let client be able to send us
    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    int flag = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag)) < 0){
        cerr << "reuse addr fail.\n";
        exit(0);
    }

    // add "::"before""bind" because bind func is same as std's
    if (::bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))<0){
        cerr << "server: can't bind local address" << endl;
    }

    listen(sockfd, 5);

    for(;;){
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd<0) {
            cerr << "server: accept error" << endl;
        }
        childpid = fork();
        if(childpid == 0){ // child
            close(sockfd);
            dup2(newsockfd, STDIN_FILENO);
            dup2(newsockfd, STDOUT_FILENO);
            dup2(newsockfd, STDERR_FILENO);

            shell();
            break;
        }
        else if(childpid > 0){ // parent
            close(newsockfd);
            waitpid(childpid,&sock_status,0);
        }
        else {
            cerr << "server: fork error" << endl;
        }

    }
    return 0;
}

int main(int argc, char* argv[]) {

    build_socket(atoi(argv[1]));

    return 0;
}
