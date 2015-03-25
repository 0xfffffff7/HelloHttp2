#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#pragma warning(disable:4996)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#define SOCKET int
#define SD_BOTH SHUT_WR
#endif


#define READ_BUF_SIZE 4096
#define BUF_SIZE 4097
#define PORT 80
#define BINARY_FRAME_LENGTH 9


// 3バイトのネットワークオーダーを4バイト整数へ変換する関数.
char* to_framedata3byte(char *p, int &n);

int get_error();

void close_socket(SOCKET socket);

int main(int argc, char **argv)
{
    
    
    //------------------------------------------------------------
    // 接続先ホスト名.
    // HTTP2に対応したホストを指定します.
    //------------------------------------------------------------
    std::string host = "nghttp2.org";
    
    
    
    
    //------------------------------------------------------------
    // TCPの準備.
    //------------------------------------------------------------
#ifdef WIN32
    WSADATA	wsaData;
    // WinSock?I?‰?u‰≫.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }
#endif
    
    int error = 0;
    struct hostent *hp;
    struct sockaddr_in addr;
    SOCKET _socket;
    
    if (!(hp = gethostbyname(host.c_str()))){
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr = *(struct in_addr*)hp->h_addr_list[0];
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    
    if ((_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))<0){
        return -1;
    }
    if (connect(_socket, (struct sockaddr *)&addr, sizeof(addr))<0){
        return -1;
    }
    
    
    
    //------------------------------------------------------------
    // HTTP2の準備.
    // SSLでない場合は、下記の24オクテットのデータを送信することで、
    // これからHTTP2通信を始めることを伝える.
    //------------------------------------------------------------
    std::string pri = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    int r = (int)::send(_socket, pri.c_str(), pri.length(), NULL);
    

    //------------------------------------------------------------
    // 全てのデータはバイナリフレームで送受信される
    // バイナリフレームは共通の9バイトヘッダと、データ本体であるpayloadを持つ
    //
    // ●ヘッダ部分のフォーマット
    //   1?3バイト  payloadの長さ。長さにヘッダの9バイトは含まれない。.
    //   4バイト　フレームのタイプ.
    //   5バイト　フラグ.
    //   6?9バイト　ストリームID.
    //------------------------------------------------------------
    
    //------------------------------------------------------------
    // Settingフレームの送信.
    // フレームタイプは「0x04」
    // 全てデフォルト値を採用するためpayloadは空です。
    //------------------------------------------------------------
    const char settingframe[BINARY_FRAME_LENGTH] = { 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
    
    r = (int)::send(_socket, settingframe, BINARY_FRAME_LENGTH, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    
    //------------------------------------------------------------
    // Settingフレームの受信.
    //------------------------------------------------------------
    char buf[BUF_SIZE] = { 0 };
    char* p = buf;
    
    r = (int)::recv(_socket, p, READ_BUF_SIZE, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    
    
    //------------------------------------------------------------
    // ACKの送信.
    // ACKはSettingフレームを受け取った側が送る必要がある.
    // ACKはSettingフレームのフラグに0x01を立ててpayloadを空にしたもの.
    //
    // フレームタイプは「0x04」
    // 5バイト目にフラグ0x01を立てます。
    //------------------------------------------------------------
    const char settingframeAck[BINARY_FRAME_LENGTH] = { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 };
    
    r = (int)::send(_socket, settingframeAck, BINARY_FRAME_LENGTH, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    
    //------------------------------------------------------------
    // HEADERSフレームの送信.
    //
    // フレームタイプは「0x01」
    // このフレームに必要なヘッダがすべて含まれていることを示すために5バイト目にフラグ0x04を立てます。
    //
    // ●HTTP1.1でのセマンティクス
    // 　　"GET / HTTP1/1"
    // 　　"Host: nghttp2.org
    //
    // ●HTTP2でのセマンティクス
    //		:method GET
    //		:path /
    //		:scheme http
    //		:authority nghttp2.org
    //
    // 本来HTTP2はHPACKという方法で圧縮しますが、
    // 今回は上記のHTTP2のセマンティクスを圧縮なしで記述します.
    //
    // 一つのヘッダフィールドの記述例
    //
    // |0|0|0|0|      0|   // 最初の4ビットは圧縮に関する情報、次の4ビットはヘッダテーブルのインデクス.(今回は圧縮しないのですべて0)
    // |0|            7|   // 最初の1ビットは圧縮に関する情報(今回は0)、次はフィールドの長さ
    // |:method|           // フィールドをそのままASCIIのオクテットで書く。
    // |0|            3|   // 最初の1ビットは圧縮に関する情報(今回は0)、次はフィールドの長さ
    // |GET|               // 値をそのままASCIIのオクテットで書く。
    //
    // 上記が一つのヘッダフィールドの記述例で、ヘッダーフィールドの数だけこれを繰り返す.
    //
    //------------------------------------------------------------
    const char headersframe[69] = { 0x00, 0x00, 0x3c, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x3a, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x03, 0x47, 0x45, 0x54, 0x00, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x01,
        0x2f, 0x00, 0x07, 0x3a, 0x73, 0x63, 0x68, 0x65, 0x6d, 0x65, 0x04, 0x68, 0x74, 0x74, 0x70, 0x00,
        0x0a, 0x3a, 0x61, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79, 0x0b, 0x6e, 0x67, 0x68, 0x74, 0x74, 0x70, 0x32,
        0x2e, 0x6f, 0x72, 0x67 };
    // ..:........ :method.GET.. : path.
    // /..:scheme.http..:authority:.nghttp2
    // .org
    
    r = (int)::send(_socket, headersframe, 69, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    
    //------------------------------------------------------------
    // HEADERSフレームの受信.
    //------------------------------------------------------------
    int payload_length = 0;
    int frame_type = 0;
    
    // まずはヘッダフレームを受信してpayloadのlengthを取得する。
    while (1){
        
        memset(buf, 0x00, BINARY_FRAME_LENGTH);
        p = buf;
        
        r = (int)::recv(_socket, p, BINARY_FRAME_LENGTH, NULL);
        if (r == -1){
            error = get_error();
            ::shutdown(_socket, SD_BOTH);
            close_socket(_socket);
            return 0;
        }
        
        // ACKが返ってくる場合があるのでACKなら無視して次を読む。
        if (memcmp(buf, settingframeAck, BINARY_FRAME_LENGTH) == 0){
            continue;
        }
        else{
            
            // payloadの長さを取得する。
            p = to_framedata3byte(p, payload_length);
            
            // フレームタイプがHEADERS_FRAMEではなかったら読み飛ばす。
            memcpy(&frame_type, p, 1);
            if (frame_type != 1){
                
                r = (int)::recv(_socket, p, payload_length, NULL);
                if (r == -1){
                    error = get_error();
                    ::shutdown(_socket, SD_BOTH);
                    close_socket(_socket);
                    return 0;
                }
                
                continue;
            }
            break;
        }
    }
    
    // 次にHEADERSフレームのpayloadを受信する。
    memset(buf, 0x00, payload_length);
    p = buf;
    r = (int)::recv(_socket, p, payload_length, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    
    //------------------------------------------------------------
    // DATAフレームの受信.
    //------------------------------------------------------------
    
    // まずはヘッダフレームを受信してpayloadのlengthを取得する。
    memset(buf, 0x00, BINARY_FRAME_LENGTH);
    p = buf;
    r = (int)::recv(_socket, p, BINARY_FRAME_LENGTH, NULL);
    to_framedata3byte(p, payload_length);
    
    // 次にpayloadを受信する。
    while (payload_length > 0){
        
        memset(buf, 0x00, BUF_SIZE);
        p = buf;
        
        r = (int)::recv(_socket, p, READ_BUF_SIZE, NULL);
        if (r == -1){
            error = get_error();
            ::shutdown(_socket, SD_BOTH);
            close_socket(_socket);
            return 0;
        }
        payload_length -= r;
        
        printf("%s", p);
    }
    
    
    //------------------------------------------------------------
    // GOAWAYの送信.
    //
    // これ以上データを送受信しない場合はGOAWAYフレームを送信します.
    //------------------------------------------------------------
    const char goawayframe[17] = { 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
    
    r = (int)::send(_socket, goawayframe, 17, NULL);
    if (r == -1){
        error = get_error();
        ::shutdown(_socket, SD_BOTH);
        close_socket(_socket);
        return 0;
    }
    
    

    //------------------------------------------------------------
    // 後始末.
    //------------------------------------------------------------
    ::shutdown(_socket, SD_BOTH);
    close_socket(_socket);
    
    return 0;
    
}

void close_socket(SOCKET socket){
#ifdef WIN32
    ::closesocket(socket);
    WSACleanup();
#else
    ::close(socket);
#endif
}

int get_error(){
#ifdef WIN32
    return WSAGetLastError();
#endif
    return errno;
}

char* to_framedata3byte(char *p, int &n){
    u_char buf[4] = { 0 };
    memcpy(&(buf[1]), p, 3);
    memcpy(&n, buf, 4);
    n = ntohl(n);
    p += 3;
    return p;
}

