#ifndef NP_SINGLE_PROC_H
#define NP_SINGLE_PROC_H

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

#define INITIAL_ENV  34
#define WHO_PROC  35
#define TELL_PROC 36
#define YELL_PROC 37
#define NAME_PROC 38

//erase TYPE
#define ERASE_CMD_FOR_CMDSEG  40
#define ERASE_CMD_WHILE_LINE  41

//transfer direction
#define GET_USERID 50
#define GET_SOCKFD 51

//get userpipe's pipefd
#define GET_OUTPIPE    60
#define GET_ENTERPIPE  61

//erase userpipe type
#define CAUSE_USED   70   ////when client exist, close related fd and erase related userPipe in userPipeTable (vector)
#define CAUSE_EXIST  71   ////when shell->process complete
                          ////close all fd in parent and erase specific userPipeinfo in userPipeTable (vector)
                          ////so socket can close


#define BUF_SIZE 15000

int devNull_fd = open("/dev/null", O_RDWR);

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

        bool isUserPipe_i = false; // >2
        bool isUserPipe_o = false; // >2
        int userPipeI_senderID;
        int userPipeO_recverID;
        string userPipeI_sucMsg = "";
        string userPipeO_sucMsg = "";

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

class USERPIPE{
    public:
        int senderUserID;
        int recverUserID;
        int enterPipe;
        int outPipe;
};

class ENV{
    public:
        string name;
        string value;
};

class CLIENT{
    public:
        int ssockfd = -1;
        int userID  = -1;
        string userName = "";
        string IPnPort  = "";
        vector<ENV> envTable;
        // vector<int> sendRequest;   // >who.
        // vector<int> recvRequest;   // <who.

        bool exit_status = false;

        vector<PIPE>    pipeTable;
        vector<pid_t>   pidList;
};


vector<USERPIPE> userPipeTable; // store alll user pipe
CLIENT Clients[30]; //all client in server


int printCmdsValue(vector<COMMAND> Cmds);
int printPipeTable(vector<PIPE> &pipeTable);

vector<COMMAND> setCmdsInOneLine(int ssockfd, vector<string> items);
vector<string>  getItemsInOneLine(int ssockfd);
vector<string>  getCmdSeg(int idx_CmdSeg, int cnt_CmdSeg, vector<string> vec);
int  cntCmdSeg(vector<string> vec);
int* getPipeType(vector<string> vec);


int process(int processType, vector<string> cmd, int ssockfd);

int makePipe(COMMAND Cmd, vector<PIPE> &pipeTable);

int setCmdFd(int isBuiltPipe, COMMAND &Cmd, vector<PIPE> &pipeTable);
bool isStdinFromPrevLine(vector<PIPE> &pipeTable, int pipeType);

int goProcessWithFork(COMMAND &Cmd, vector<PIPE> &pipeTable, vector<USERPIPE> &userPipeTable, int ssockfd);

int erasePipes(int type, vector<PIPE> &pipeTable, COMMAND cmd);
int closeAllPipes();
int subPipeCrossLine(vector<PIPE> &pipeTable);

int writeWelcomeMsg(int ssockfd);

int shell(int ssockfd, vector<COMMAND> Cmds);

int setupServer(int port);
int accept_new_connection(int msockfd);
int initial_client_data(int ssockfd, struct sockaddr_in cli_addr);

int isUserPipeRequest(int ssockfd, vector<string> &items, bool *isUserPipe_i, bool *isUserPipe_o,
                      int *userPipeI_ID, int *userPipeO_ID,
                      string *userPipeI_sucMsg, string *userPipeO_sucMsg,
                      bool *isSenderNotExist, bool *isRecverNotExist
                      );

int makeUserPipe(int senderID, int recverID);
int seekUserPipe(int senderID, int recverID, int type);

int erase_client_data(int userID);
int eraseUserPipe(vector<USERPIPE> &userPipeTable, int senderID, int recverID, int type);
int closeAllUserPipes(vector<USERPIPE> userPipeTable);


int ssockFd_userId(int src, int type);
int broadcast(string msg);

bool check(int exp, string err_msg);
bool isUserExist(int userID);
bool isPipeExist(int senderID, int recverID);

int printClients();
int printUserPipeTable(vector<USERPIPE> &userPipeTable);


#endif