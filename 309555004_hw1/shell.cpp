// #include "shell.h"

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
#include <stdio.h>

using namespace std;

#define NOPIPE  10
#define ORDPIPE 11
#define NUMPIPE 12
#define IN2PIPE 13
#define REDIRPIPE 14


#define CmdAtBegin 20
#define CmdInMid 21
#define CmdAtEnd 22

#define Demo_append 31
#define Demo_unsetenv 32
#define Demo_history 33
#define Demo_status 34
#define Demo_leftPipe 35
#define Demo_ignore 36
#define Demo_plus 37
#define Demo_source 38

#define BUF_SIZE 15000

#define F_HIS_PATH "./npshell_history.txt"
#define F_APP_PATH "./append.txt"


int DemoMode = 0;

int exit_command = 0;
int pipe_end = 0;
int status;

bool debug_mode = false;
FILE *fp = NULL;


int Openfile(){
    if(DemoMode == Demo_history){
        fp = NULL;
        fp = fopen(F_HIS_PATH,"w");
        if(fp == NULL) return -1;
    }
    if(DemoMode == Demo_append){
        fp = NULL;
        fp = fopen(F_APP_PATH,"w");
        if(fp == NULL) return -1;
    }
    return 0;
}

int CloseFile(){
    if(DemoMode == Demo_history){
        fclose(fp);
        fp = NULL;

    }
    return 0;
}

int WriteFile(string str){
    if(DemoMode == Demo_history){
        // cout << str ;
        fprintf(fp, "%s", str.c_str());
    }
    return 0;
}


class COMMAND{
    public:
        vector<string> CmdSeg; //cat test.html
        int argc = 1;
        int pipeType = NOPIPE;
        int cmdPos = CmdAtBegin;
        int readFd = STDIN_FILENO;
        int writeFd = STDOUT_FILENO;
        int errorFd = STDERR_FILENO;
        bool is_redirFileName = false;
        vector<string> redirFileName;
        bool is_stdin_from_prev_line = false;
        //is_stdout_cross_line, is_stderr_cross_line, cnt_cross_line
        //set in setCmdsInOneLine()
        bool is_stdout_cross_line = false;
        bool is_stderr_cross_line = false;
        int cnt_cross_line = 0;
};

class PIPE{
    public:
        int enterPipe;
        int outPipe;
        int cntLine = 0;
        bool is_redirPipe = false;
};

void printCmdsValue(vector<COMMAND> Cmds){
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
        // cerr << Cmds[idx].is_stdout_cross_line << endl;
        // cerr << Cmds[idx].is_stderr_cross_line << endl;
        // cerr << Cmds[idx].cnt_cross_line << endl;
        cerr << endl;
    }
}

int printPipeTable(vector<PIPE> &pipeTable);

vector<COMMAND> Cmds; //cmds in one line
vector<pid_t> pidList;

vector<string> getItemsInOneLine(){
    string line;
    string tmp;
    vector<string> vec;

    getline(cin,line);
    WriteFile(line);
    WriteFile("\n");
    stringstream input(line);
    while(input>>tmp){ //split line to string tmp
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
            // cout << ele << endl;
            cnt_CmdSeg++;
        }
    }
    return cnt_CmdSeg;
}

int* getPipeType(int cnt_CmdSeg, vector<string> vec){
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
    // cout << idx_CmdSeg << " " << cnt_CmdSeg << endl;
    int index=0;
    vector<string> vecSeg;
    for (int i=0; i<vec.size(); i++){ //run all vec items
        if (index==idx_CmdSeg){ //in the same Segment
            // cout << "same segment" << endl;
            for (int j=i; j<vec.size(); j++){
                string ele = vec[j];
                if (int(ele[0])!=124 && int(ele[0])!= 33 && int(ele[0])!= 62) { // "|":124, "!":33, ">":62
                    vecSeg.push_back(ele);
                }
                else {
                    // cout << "return: ";
                    return vecSeg;
                }
            }
            break;
        }
        else { //find Segment
            string ele = vec[i];
            // cout << "fin segment: " << ele << endl;
            if ((int(ele[0])==124  && i!=vec.size()-1) || int(ele[0])== 62) { // "|":124, "!":33, ">":62
                index++;
                // if (i+1 == vec.size() && (int(ele[0])==124 || int(ele[0])== 33)){ // for case like |4, !4
                //     string ele = vec[i];
                //     vecSeg.push_back(ele);
                //     return vecSeg;
                // }
            }
        }
    }
    return vecSeg;
}

vector<COMMAND> setCmdsInOneLine(){
    vector<string> items = getItemsInOneLine();
    int cnt_CmdSeg = cntCmdSeg(items);
    int *pipeType = getPipeType(cnt_CmdSeg, items);
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

int bin_process(vector<string> cmd){
    if(debug_mode) cerr << "bin_process" << endl;

    int argc = cmd.size(); //ex. 3
    char *argv[argc+1]; // argv[4] = ["cat", "t1.txt", "t2.txt", NULL]
    argv[argc] = NULL;
    for (int i=0; i<argc; i++){
        string str = cmd[i];
        argv[i] = (char*)malloc(str.size()+1);
        strcpy(argv[i],str.c_str());
        //cout << "argv[" << i << "]: "<< argv[i] << endl;
    }

    string str = cmd[0];
    if(execvp(str.c_str(), argv)==-1){
        cerr << "Unknown command: [" << str << "]." << endl;
    }
    return 0;
}

int setenv_process(vector<string> cmd){
    string str1 = cmd[1];
    string str2 = cmd[2];
    char *argv1 = new char[str1.size()+1];
    char *argv2 = new char[str2.size()+1];
    strcpy(argv1, str1.c_str());
    strcpy(argv2, str2.c_str());
    setenv(argv1,argv2, 1);
    return 0;
}

int unsetenv_process(vector<string> cmd){
    string str = cmd[1];
    char *argv = new char[str.size()+1];
    strcpy(argv, str.c_str());
    setenv(argv, "", 1);
    return 0;
}

int printenv_process(vector<string> cmd){
    string str = cmd[1];
    // cout << str << endl;
    char *argv = new char[str.size()+1];
    strcpy(argv, str.c_str());
    if (getenv(argv) != NULL) {
        cout << getenv(argv) << endl;
    }

    return 0;
}

int exit_process(vector<string> cmd){
    return 1;
}

int makeRedirPipe(vector<string> filename){
    FILE *wfile;
    wfile = fopen(filename[0].c_str(), "w");

    if(wfile == NULL){
        perror("Open file error!");
    }
    int wFd = fileno(wfile);
    return wFd;
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

int makePipe(COMMAND Cmd, vector<PIPE> &pipeTable){
    //check need make pipe or not
    ////if no need pipe
    if (Cmd.pipeType==NOPIPE){
        return 0;
    }
    ////if need pipe
    else if (Cmd.pipeType==REDIRPIPE){
        int p1 = makeRedirPipe(Cmd.redirFileName);
        PIPE uniPipe;
        uniPipe.enterPipe = p1;
        uniPipe.outPipe = STDOUT_FILENO;
        uniPipe.is_redirPipe = true;
        pipeTable.push_back(uniPipe);
        if(debug_mode) cerr << "  build redirPipe, p1= "<< p1 << endl;
    }
    else if(Cmd.pipeType==NUMPIPE || Cmd.pipeType==IN2PIPE){
        // when cntLine same, use same pipe
        int need_build_pipe_flag = 1;
        vector<PIPE>::iterator itr = pipeTable.begin();

        while(itr != pipeTable.end()){
            if((*itr).cntLine==Cmd.cnt_cross_line){
                need_build_pipe_flag = 0;
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
        // printPipeTable(pipeTable);

    }
    return 1;
}

int setCmdFd(COMMAND &Cmd, vector<PIPE> &pipeTable){
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
        // cerr << "  itrI: "<< (*itrI).enterPipe << " "<< (*itrI).outPipe << " "<<  (*itrI).cntLine << endl;
        Cmd.readFd = (*itrI).outPipe;

        ////pipe out
        vector<PIPE>::iterator itrO = pipeTable.end()-1;
        // cerr << "  itrO: "<< (*itrO).enterPipe << " "<< (*itrO).outPipe << " "<<  (*itrO).cntLine << endl;

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
        else if (Cmd.pipeType!=NOPIPE) {
            // NUMPIPE || IN2PIPE


            int need_build_pipe_flag = 1;
            vector<PIPE>::iterator itrBuiltPipe = pipeTable.begin();
            while(itrBuiltPipe != pipeTable.end()){
                if((*itrBuiltPipe).cntLine==Cmd.cnt_cross_line){
                    need_build_pipe_flag = 0;
                }
                itrBuiltPipe++;
            }

            //if NUMPIPE || IN2PIPE是共用別人的
            if (need_build_pipe_flag==0){
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
            //else NUMPIPE || IN2PIPE是新創出來的
            else {
                vector<PIPE>::iterator itrI = pipeTable.end()-2;
                Cmd.readFd = (*itrI).outPipe;

                vector<PIPE>::iterator itrO = pipeTable.end()-1;
                Cmd.writeFd = (*itrO).enterPipe;
                if (Cmd.pipeType==IN2PIPE){
                    Cmd.errorFd = (*itrO).enterPipe;
                }
            }
        }
    }
    if(debug_mode) cerr << "  setCmd: Cmd.readFd=" << Cmd.readFd << ", Cmd.writeFd=" << Cmd.writeFd << ", Cmd.errorFd=" << Cmd.errorFd << endl;
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

int goProcessWithFork(COMMAND &Cmd, vector<PIPE> &pipeTable){

    int rFd = Cmd.readFd;
    int wFd = Cmd.writeFd;
    int eFd = Cmd.errorFd;
    int cmdPos = Cmd.cmdPos;
    int usingPipeType = Cmd.pipeType;

    bool is_stdin = Cmd.is_stdin_from_prev_line;
    // bool is_stderr = Cmd.is_stderr_from_prev_line;

    vector<string> CmdSeg = Cmd.CmdSeg;

    pid_t pid;
    while((pid = fork()) < 0){
        while((waitpid(-1, &status, WNOHANG)) > 0){

        }
    }
    pidList.push_back(pid);

    if (pid==0){
        if(debug_mode) cerr << "  child rFd=" << rFd << ", wFd=" << wFd << ", eFd=" << eFd << endl;

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
            bin_process(CmdSeg);
        }

        exit(-1);
    }
    else if (pid>0){
        //parent
    }
    else {
        if(debug_mode) cerr << "fork error" << endl;
    }
    return 0;
}

int erasePipeForCmdsInSameLine(vector<PIPE> &pipeTable, COMMAND cmd){
    // erase pipe but redir
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


int erasePipewhileLine(vector<PIPE> &pipeTable){
//erase Pipe Except CrossLine
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
            // close((*itr).enterPipe);
            // close((*itr).outPipe);
            // itr = pipeTable.erase(itr);
        }
    }
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

int main() {
    Openfile();
    if(setenv("PATH","bin:.", 1)!=0){
        if(debug_mode) cerr << "setenv err" << endl;
    }
    vector<PIPE> pipeTable;
    while(1){ //line loop
        waitpid(-1, &status, WNOHANG);
        pipe_end = 0;

        cout << "% ";
        WriteFile("% ");

        Cmds = setCmdsInOneLine();

        if (debug_mode){
            printCmdsValue(Cmds);
        }

        if (DemoMode==Demo_unsetenv && Cmds[0].CmdSeg[0] == "unsetenv"){
            unsetenv_process(Cmds[0].CmdSeg);
            continue;
        }


        for (int idx = 0; idx<Cmds.size(); idx++){ //handle one Cmd Segment at once
            if(debug_mode) cerr << "idx=" << idx << ", cnt_CmdSeg=" << Cmds.size() << endl;
            if (idx==Cmds.size()-1){
                pipe_end = 1;
            }

            string ele = Cmds[idx].CmdSeg[0];
            if (ele.compare("printenv")==0){
                printenv_process(Cmds[idx].CmdSeg);
                continue;
            }
            else if (ele.compare("setenv")==0){
                setenv_process(Cmds[idx].CmdSeg);
                continue;
            }
            else if (ele.compare("exit")==0){
                exit_command = exit_process(Cmds[idx].CmdSeg);
                return 0;
            }
            // else if (DemoMode==Demo_unsetenv && ele.compare("unsetenv")==0){
            //     unsetenv_process(Cmds[idx].CmdSeg);
            //     continue;
            // }


            int ret = makePipe(Cmds[idx], pipeTable); //return value = 0 (no produced pipe) or 1 (produced pipe)
            if(debug_mode) cerr << "makePipe done = " << ret << ", pipeTable.size()=" << pipeTable.size() << endl;

            if (Cmds[idx].is_redirFileName==false){
                setCmdFd(Cmds[idx], pipeTable); //set Cmd.readFd, Cmd.writeFd, Cmd.errorFd,
                if(debug_mode) cerr << "setCmdFd done" << endl;

                goProcessWithFork(Cmds[idx], pipeTable); //dup Pipe
                if(debug_mode) cerr << "goProcessWithFork done" << endl;

            }
            ////waitpid
            for(int i=0;i<100;i++) waitpid(-1, &status, WNOHANG);

            ////close fd in parent
            erasePipeForCmdsInSameLine(pipeTable, Cmds[idx]);
            if(debug_mode) cerr << "erasePipe done, pipeTable.size()="<< pipeTable.size() << endl;

            if(debug_mode) printPipeTable(pipeTable);
            if(debug_mode) cerr<< endl;
        }

        erasePipewhileLine(pipeTable);

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

        //reserve numpipe & in2pipe , minus line
        subPipeCrossLine(pipeTable);
        Cmds.clear();
    }
    CloseFile();
    return 0;
}
