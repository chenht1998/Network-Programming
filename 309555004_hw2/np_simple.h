#ifndef SHELL_H
#define SHELL_H

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

using namespace std;

#define CLIENT_EXIT 3

//PIPE TYPE
#define NOPIPE  10
#define ORDPIPE 11
#define NUMPIPE 12
#define IN2PIPE 13
#define REDIRPIPE 14

//COMMAND POSITION
#define CmdAtBegin 20
#define CmdInMid 21
#define CmdAtEnd 22

//PROCESS TYPE
#define BIN_PROCESS  30
#define SETENV_PROCESS 31
#define PRINTENV_PROCESS 32
#define EXIT_PROCESS 33


//erase TYPE
#define ERASE_CMD_FOR_CMDSEG 40
#define ERASE_CMD_WHILE_LINE  41

#define BUF_SIZE 15000

class COMMAND{
    public:
        vector<string> CmdSeg; //cat test.html
        int  argc = 1;
        int  pipeType = NOPIPE;
        int  cmdPos   = CmdAtBegin;
        int  readFd   = STDIN_FILENO;
        int  writeFd  = STDOUT_FILENO;
        int  errorFd  = STDERR_FILENO;

        int  cnt_cross_line = 0;
        bool is_stdin_from_prev_line = false;
        bool is_stdout_cross_line = false;
        bool is_stderr_cross_line = false;

        bool is_redirFileName     = false;
        vector<string> redirFileName;

        //is_stdout_cross_line, is_stderr_cross_line, cnt_cross_line
        //set in setCmdsInOneLine()
};

class PIPE{
    public:
        int enterPipe;
        int outPipe;
        int cntLine = 0;
        bool is_redirPipe = false;
};

vector<COMMAND> Cmds; //cmds in one line
vector<PIPE>    pipeTable;
vector<pid_t>   pidList;


int printCmdsValue(vector<COMMAND> Cmds);
int printPipeTable(vector<PIPE> &pipeTable);


vector<COMMAND> setCmdsInOneLine();
vector<string>  getItemsInOneLine();
vector<string>  getCmdSeg(int idx_CmdSeg, int cnt_CmdSeg, vector<string> vec);
int  cntCmdSeg(vector<string> vec);
int* getPipeType(vector<string> vec);


int process(int processType, vector<string> cmd);

int makePipe(COMMAND Cmd, vector<PIPE> &pipeTable);

int setCmdFd(int isBuiltPipe, COMMAND &Cmd, vector<PIPE> &pipeTable);
bool isStdinFromPrevLine(vector<PIPE> &pipeTable, int pipeType);

int goProcessWithFork(COMMAND &Cmd, vector<PIPE> &pipeTable);

int erasePipes(int type, vector<PIPE> &pipeTable, COMMAND cmd);
int closeAllPipes(vector<PIPE> pipeTable);
int subPipeCrossLine(vector<PIPE> &pipeTable);

int coutWelcomeMsg(char *ip);

int shell();
int build_socket(int port);

#endif