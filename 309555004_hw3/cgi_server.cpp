#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <cstdlib>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <string.h>
#include <string>

#include <vector>


using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_service global_io_service;

class REQUEST{
    public:
        string METHOD;
        string URI;
        string QUERY_STRING;
        string SERVER_PROTOCOL;
        string HTTP_HOST;
        string SERVER_ADDR;
        string SERVER_PORT;
        string REMOTE_ADDR;
        string REMOTE_PORT;
};

REQUEST reqCtxt;

REQUEST parse(char* request);
REQUEST parseEnvVar(vector<string> ctxt);
vector<string> splitRequest(char* request);
void printReqCtxt(REQUEST req);

class SERVER {
    public:
        string host;
        string port;
        string file;

        fstream openfile;
        int hostID;
};

SERVER npHost[5];

int    hostParse(string query_str);
bool   check_read_done(string oriStr);
string getSubStrVal(string ori);
string consoleCGI(int nHost);
string output_command(string host, string content);
string output_shell(string host, string content);
string replaceSubStr(string oriStr);
string panelCGI();

class ConsoleSession  : public enable_shared_from_this<ConsoleSession>{
    private:
        tcp::resolver resolver_;
        tcp::socket socket_;
        shared_ptr<tcp::socket> http_socket_;

        int hostIdx_;
        enum { max_length = 4096 };
        char data_[max_length] = {0};
        bool is_exit = false;

        const int SHELL = 10;
        const int CMD = 11;

    public:
        ConsoleSession(int hostIdx, shared_ptr<tcp::socket> http_socket) : hostIdx_(hostIdx), http_socket_(http_socket), resolver_(global_io_service), socket_(global_io_service) {
        }

        void start() {
            do_resolve();
        }

    private:
        void do_resolve() {
            auto self(shared_from_this());
            tcp::resolver::query q(npHost[hostIdx_].host, npHost[hostIdx_].port);
            resolver_.async_resolve(q,
                                    [this, self](boost::system::error_code ec, tcp::resolver::iterator itr){
                                        if (ec) cerr << "Error (async_resolve): " << ec.message() << ", host= " << npHost[hostIdx_].host+":"+npHost[hostIdx_].port << "\n";
                                        else  do_connect(itr); });

        }

        void do_connect(tcp::resolver::iterator itr){
            auto self(shared_from_this());
            tcp::endpoint endpoint = *itr;
            socket_.async_connect(endpoint,
                                  [this, self](boost::system::error_code ec){
                                        if (ec) cerr << "Error (async_connect): " << ec.message() << "\n";
                                        else  do_read(); });
        }

        void do_read() {
            auto self(shared_from_this());
            memset ( data_, '\0' , max_length );
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {

                                        if (ec == boost::asio::error::eof) {
                                            do_write_web(SHELL, hostIdx_, data_);
                                            do_read();
                                        }
                                        else if(ec) cerr << "Error (do_read): " << ec.message() << "\n";
                                        else {
                                            do_write_web(SHELL, hostIdx_, data_);
                                            if ( check_read_done(data_) )  {
                                                string cmd = "";
                                                getline(npHost[hostIdx_].openfile, cmd);
                                                cmd += "\n";

                                                do_write_http(hostIdx_, cmd);
                                                do_write_web(CMD, hostIdx_, cmd);

                                                if (cmd=="exit\n") {
                                                    is_exit = true;
                                                    (npHost[hostIdx_].openfile).close();
                                                }
                                            }
                                            if (is_exit == false ) do_read();
                                        }
                                    });
        }

        void do_write_http(int hostID, string context) {
            auto self(shared_from_this());

            socket_.async_write_some(boost::asio::buffer(context.c_str(), context.length()),
                                     [this, self](boost::system::error_code ec, std::size_t) {
                                        if(ec)  { cerr << "Error (async_write_some): " << ec.message() << "\n"; return;}
                                     });
        }

        void do_write_web(int type, int hostID, string context) {
            auto self(shared_from_this());
            string str = "";
            if(type == SHELL) str = output_shell("s"+to_string(hostID), context);
            if(type == CMD)   str = output_command("s"+to_string(hostID), context);

            http_socket_->async_write_some(boost::asio::buffer(str.c_str(), str.length()),
                                     [this, self](boost::system::error_code ec, std::size_t) {
                                        if (ec)  cerr << "Error (async_write_some): " << ec.message() << "\n";
                                        return;
                                     });

        }

};

class http_session  :  public std::enable_shared_from_this<http_session>{
    private:
        tcp::socket socket_;
        enum { max_length = 4096 };
        char data_[max_length];

    public:
        http_session(tcp::socket socket) : socket_(std::move(socket)) { }
        void http_start() {
            http_do_read();
        }

    private:
        void http_do_read() {
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {
                                        if (ec) { cerr << "Error (async_read_some): " << ec.message() << "\n"; return;}

                                        cerr << "data_: \n" << data_  << "data_ end\n\n";
                                        string _200_OK =  reqCtxt.SERVER_PROTOCOL+" 200 OK\r\n";
                                        do_write(_200_OK);
                                        reqCtxt = parse(data_);
                                        reqCtxt.REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
                                        reqCtxt.REMOTE_PORT = std::to_string(socket_.remote_endpoint().port());
                                        printReqCtxt(reqCtxt);

                                        if (data_[5] == 'p'){
                                            // string _200_OK =  reqCtxt.SERVER_PROTOCOL+" 200 OK\r\n";
                                            // do_write(_200_OK);
                                            do_write_panel(panelCGI());
                                            memset ( data_, '\0' , max_length );

                                        }

                                        if (data_[5] == 'c'){
                                            printReqCtxt(reqCtxt);
                                            cerr << "reqCtxt.Query = " << reqCtxt.QUERY_STRING << endl;
                                            int nHost = hostParse(reqCtxt.QUERY_STRING);
                                            do_write(consoleCGI(nHost));
                                            cerr << "nHost = " << nHost << endl;
                                            do_console_cgi(nHost);
                                        }


                                        } );
        }

        void do_write_panel(string str) {
            auto self(shared_from_this());
            socket_.async_write_some(boost::asio::buffer(str.c_str(), str.length()),
                                                        [this, self](boost::system::error_code ec, std::size_t) {
                                                            if (ec)  cerr << "Error (async_write_some): " << ec.message() << "\n";
                                                            // socket_.close();
                                                            cerr << "here " << endl;
                                                        });
        }

        void do_write(string str) {
            auto self(shared_from_this());
            socket_.async_write_some(boost::asio::buffer(str.c_str(), str.length()),
                                                        [this, self](boost::system::error_code ec, std::size_t) {
                                                            if (ec)  cerr << "Error (async_write_some): " << ec.message() << "\n";
                                                        });
        }


        void do_console_cgi(int nHost){
            auto self(shared_from_this());
            try {
                shared_ptr<tcp::socket> socket_ptr(&socket_);
                for (int i=0; i<nHost; i++)  {
                    make_shared<ConsoleSession>(i, socket_ptr)->start();
                }
                global_io_service.run();
            }

            catch (std::exception& e) {
                cerr << "Exception: " << e.what() << "\n";
            }
        }

};

class http_server {
    private:
        tcp::acceptor acceptor_;
        // tcp::socket socket_;


    public:
        http_server(short port) : acceptor_(global_io_service, tcp::endpoint(tcp::v4(), port)){//, socket_(global_io_service) {
            do_accept();
        }

    private:
        void do_accept() {
            acceptor_.async_accept(
                                    [this](boost::system::error_code ec, tcp::socket socket_) {
                                        if (!ec) {
                                            std::make_shared<http_session>(std::move(socket_))->http_start();

                                        }
                                        do_accept();
                                    } );
        }
};

int main(int argc, char* argv[]){
    try {
        if (argc != 2) {
            cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        http_server http_s(std::atoi(argv[1]));
        global_io_service.run();
    }

    catch (std::exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

void printReqCtxt(REQUEST req){
    cerr << "REQUEST_METHOD=" << req.METHOD << endl;
    cerr << "REQUEST_URI=" << req.URI << endl;
    cerr << "QUERY_STRING=" << req.QUERY_STRING << endl;
    cerr << "SERVER_PROTOCOL=" << req.SERVER_PROTOCOL << endl;
    cerr << "HTTP_HOST=" << req.HTTP_HOST << endl;
    cerr << "SERVER_ADDR=" << req.SERVER_ADDR << endl;
    cerr << "SERVER_PORT=" << req.SERVER_PORT << endl;
    cerr << "REMOTE_ADDR=" << req.REMOTE_ADDR << endl;
    cerr << "REMOTE_PORT=" << req.REMOTE_PORT << endl;
}

REQUEST parse(char* request){
    vector<string> ctxt =  splitRequest(request);
    REQUEST req = parseEnvVar(ctxt);
    return req;
}

REQUEST parseEnvVar(vector<string> ctxt){
    REQUEST req;
    vector<string>::iterator itr = ctxt.begin();

    // REQUEST_METHOD
    req.METHOD = *itr;  // GET or POST or else
    itr++;

    // REQUEST_URI ? QUERY_STRING
    string URI_n_QUERY = *itr;
    if(strstr(URI_n_QUERY.c_str(), "?")==NULL){ // only REQUEST_URI exists
        req.URI = URI_n_QUERY;
    }
    else{ // both REQUEST_URI and QUERY_STRING exist
        req.URI = strtok(const_cast<char *>(URI_n_QUERY.c_str()), "?"); //const_cast可以跨過 const 訪問原始變數
        req.QUERY_STRING = strtok(NULL, " \n\r");
    }
    itr++;

    // SERVER_PROTOCOL
    req.SERVER_PROTOCOL = *itr;
    itr++;

    // HTTP_HOST
    string HTTP_HOST_str;
    for (; itr!=ctxt.end(); ){
        if(*itr=="Host:"){
            itr++;
            HTTP_HOST_str = *itr;
            itr++;
            break;
        }
        else itr++;
    }

    req.SERVER_ADDR = strtok(const_cast<char *>(HTTP_HOST_str.c_str()), ":"); //const_cast可以跨過 const 訪問原始變數
    req.SERVER_PORT = strtok(NULL, " \n\r");
    req.HTTP_HOST = req.SERVER_ADDR + ":" + req.SERVER_PORT;

    return req;
}

vector<string> splitRequest(char* request){
    vector<string> vec;
    string line = request;
    string tmp;
    stringstream input(line);
    while(input>>tmp){
        vec.push_back(tmp);
    }
    return vec;
}

int hostParse(string query_str){

    int nHost = 0;

    int n = 0;
    char* query_char = const_cast<char *>(query_str.c_str());

    vector<char*> cut_vec;
    char *cut = strtok(query_char, "&");
    cerr << "vec = ";
    while( cut != NULL ) {

        cut_vec.push_back(cut);
        cerr << cut << " ";
        n++;
        cut = strtok( NULL, "&");
    }
    cerr << endl;
    for(int i=0; i<(n/3); i++){
        npHost[i].hostID = i;
        npHost[i].host = getSubStrVal(cut_vec[i*3]);
        npHost[i].port = getSubStrVal(cut_vec[i*3+1]);
        npHost[i].file = getSubStrVal(cut_vec[i*3+2]);
    }

    for(int i=0; i<(n/3); i++){
        if (npHost[i].host!="") nHost = i+1;
    }

    // npHost[i].openfile;
    for(int i=0; i<nHost; i++){
        (npHost[i].openfile).open("./test_case/"+npHost[i].file, ios::in);
    }

    for(int i=0; i<nHost; i++){
        cerr << npHost[i].hostID << endl;
        cerr << npHost[i].host << endl;
        cerr << npHost[i].port << endl;
        cerr << npHost[i].file << endl;
    }

    return nHost;
}

string getSubStrVal(string ori){
    // cerr << "getSubstrval, before_ori = " << ori ;

    string seg1 = strtok(const_cast<char *>(ori.c_str()), "=");
    char *seg;
    if((seg = strtok(NULL, "="))!=NULL) return seg;
    else  return "";

}

string consoleCGI(int nHost){
    string hdr =  "Content-type: text/html\r\n\r\n";

    string HTML_th="", HTML_td="";

    for(int i=0; i<nHost; i++)  HTML_th += "          <th scope=\"col\">" + npHost[i].host+":"+npHost[i].port +  "</th>";
    for(int i=0; i<nHost; i++)  HTML_td += "          <td><pre id=\"s"+to_string(i)+"\" class=\"mb-0\"></pre></td>";

    string HTML_TEMPLATE_1 =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "  <head>"
    "    <meta charset=\"UTF-8\">"
    "    <title>NP Project 3 Console</title>"
    "    <link"
    "      rel=\"stylesheet\""
    "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
    "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
    "      crossorigin=\"anonymous\""
    "    />"
    "    <link"
    "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
    "      rel=\"stylesheet\""
    "    />"
    "    <link"
    "      rel=\"icon\""
    "      type=\"image/png\""
    "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\""
    "    />"
    "    <style>"
    "      * {"
    "        font-family: 'Source Code Pro', monospace;"
    "        font-size: 1rem !important;"
    "      }"
    "      body {"
    "        background-color: #212529;"
    "      }"
    "      pre {"
    "        color: #cccccc;"
    "      }"
    "      b {"
    "        color: #01b468;"
    "      }"
    "    </style>"
    "  </head>"
    "  <body>"
    "    <table class=\"table table-dark table-bordered\">"
    "      <thead>"
    "        <tr>";
            // <th scope=\"col\">nplinux1.cs.nctu.edu.tw:1234</th>
            // <th scope=\"col\">nplinux2.cs.nctu.edu.tw:5678</th>
    string HTML_TEMPLATE_2 =
    "        </tr>"
    "      </thead>"
    "      <tbody>"
    "        <tr>";
            // <td><pre id=\"s0\" class=\"mb-0\"></pre></td>
            // <td><pre id=\"s1\" class=\"mb-0\"></pre></td>
    string HTML_TEMPLATE_3 =
    "        </tr>"
    "      </tbody>"
    "    </table>"
    "  </body>"
    "</html>";

    string HTML = HTML_TEMPLATE_1 + HTML_th + HTML_TEMPLATE_2 + HTML_td + HTML_TEMPLATE_3;

    return hdr + HTML;

}

bool check_read_done(string oriStr){
    if( oriStr.find("% ") != string::npos ) return true;
    else return false;
}

string output_command(string host, string content){
    cerr << "output_command "<< host << " data " << content  << endl;
    content = replaceSubStr(content);

    string command = "<script>document.getElementById('"+ host +"').innerHTML += '<b>"+ content +"</b>';</script>";
    return command;
}

string output_shell(string host, string content){
    cerr << "output_command "<< host << " data " << content  << endl;
    content = replaceSubStr(content);
    // content = "afefra1234435134ert341wed";
    string command = "<script>document.getElementById('" + host + "').innerHTML += '" + content + "';</script>\n";
    return command;
}

string replaceSubStr(string oriStr){
    boost::replace_all(oriStr, "&", "&amp;");
    boost::replace_all(oriStr, "\"", "&quot;");
    boost::replace_all(oriStr, "\'", "&apos;");
    boost::replace_all(oriStr, "\r", "");
    boost::replace_all(oriStr, ">", "&gt;");
    boost::replace_all(oriStr, "<", "&lt;");
    boost::replace_all(oriStr, "\n", "&NewLine;");
    return oriStr;
}

string panelCGI(){
    int N_SERVERS = 5;

    string FORM_METHOD = "GET";
    string FORM_ACTION = "console.cgi";

    string test_cases[10] = {"t1.txt", "t2.txt", "t3.txt", "t4.txt", "t5.txt", "t6.txt", "t7.txt", "t8.txt", "t9.txt", "t10.txt"};
    string test_case_menu = "";
    for (int i=0; i<10; i++)
        test_case_menu += "<option value=\""+test_cases[i]+"\">"+test_cases[i]+"</option>";

    string hdr = "Content-type: text/html\r\n\r\n";

    string HTML_PANEL = ""
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "  <head>"
    "    <title>NP Project 3 Panel</title>"
    "    <link"
    "      rel=\"stylesheet\""
    "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
    "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
    "      crossorigin=\"anonymous\""
    "    />"
    "    <link"
    "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
    "      rel=\"stylesheet\""
    "    />"
    "    <link"
    "      rel=\"icon\""
    "      type=\"image/png\""
    "      href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\""
    "    />"
    "    <style>"
    "      * {"
    "        font-family: 'Source Code Pro', monospace;"
    "      }"
    "    </style>"
    "  </head>"
    "  <body class=\"bg-secondary pt-5\">"

    "    <form action=\""+FORM_ACTION+"\" method=\""+FORM_METHOD+"\">"
    "      <table class=\"table mx-auto bg-light\" style=\"width: inherit\">"
    "       <thead class=\"thead-dark\">"
    "          <tr>"
    "            <th scope=\"col\">#</th>"
    "            <th scope=\"col\">Host</th>"
    "            <th scope=\"col\">Port</th>"
    "            <th scope=\"col\">Input File</th>"
    "          </tr>"
    "        </thead>"
    "        <tbody>";

    for (int i=0; i<N_SERVERS; i++){
        HTML_PANEL += ""
    "          <tr>"
    "            <th scope=\"row\" class=\"align-middle\">Session "+to_string(i+1)+"</th>"
    "            <td>"
    "              <div class=\"input-group\">"
    "                <select name=\"h"+to_string(i)+"\" class=\"custom-select\">"
    "                  <option></option>"
    "                  <option value=\"nplinux1.cs.nctu.edu.tw\">nplinux1</option><option value=\"nplinux2.cs.nctu.edu.tw\">nplinux2</option><option value=\"nplinux3.cs.nctu.edu.tw\">nplinux3</option><option value=\"nplinux4.cs.nctu.edu.tw\">nplinux4</option><option value=\"nplinux5.cs.nctu.edu.tw\">nplinux5</option><option value=\"nplinux6.cs.nctu.edu.tw\">nplinux6</option><option value=\"nplinux7.cs.nctu.edu.tw\">nplinux7</option><option value=\"nplinux8.cs.nctu.edu.tw\">nplinux8</option><option value=\"nplinux9.cs.nctu.edu.tw\">nplinux9</option><option value=\"nplinux10.cs.nctu.edu.tw\">nplinux10</option><option value=\"nplinux11.cs.nctu.edu.tw\">nplinux11</option><option value=\"nplinux12.cs.nctu.edu.tw\">nplinux12</option>"
    "                </select>"
    "                <div class=\"input-group-append\">"
    "                  <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
    "                </div>"
    "              </div>"
    "            </td>"
    "            <td>"
    "              <input name=\"p"+to_string(i)+"\" type=\"text\" class=\"form-control\" size=\"5\" />"
    "            </td>"
    "            <td>"
    "              <select name=\"f"+to_string(i)+"\" class=\"custom-select\">"
    "                <option></option>"+test_case_menu+""
    "              </select>"
    "            </td>"
    "          </tr>";
    }

    HTML_PANEL += ""
    "          <tr>"
    "            <td colspan=\"3\"></td>"
    "            <td>"
    "              <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>"
    "            </td>"
    "          </tr>"
    "        </tbody>"
    "      </table>"
    "    </form>"
    "  </body>"
    "</html>";
    // cout << HTML_PANEL << flush;
    return hdr + HTML_PANEL;
}


