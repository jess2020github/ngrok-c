#include "udp.h"
#include "ngrok.h"
#include "mytime.h"

#if UDPTUNNEL

int GetUdpRemoteAddr(int localport,char *url){
     map<string,TunnelReq*>::iterator it;
    //���е�������
    for(it=udpInfo.G_TunnelAddr.begin();it!=udpInfo.G_TunnelAddr.end();++it)
    {
        TunnelReq	*tunnelreq =(TunnelReq*)it->second;
        if(tunnelreq->localport==localport&&strncasecmp(tunnelreq->url,"udp",3)==0){
            memcpy(url,tunnelreq->url,strlen(tunnelreq->url));
            return 0;
        }
    }
    return -1;
}

int SendUdpPack(int sock, struct sockaddr_in servAddr,const char *msgstr)
{
    unsigned char buffer[strlen(msgstr)+9];
    memset(buffer,0,strlen(msgstr)+9);
    #if WIN32
    unsigned __int64 packlen;
    #else
    unsigned long long packlen;
    #endif
    packlen=strlen(msgstr);
    packlen=LittleEndian_64(packlen);
    memcpy(buffer,&packlen,8);
    memcpy(buffer+8,msgstr,strlen(msgstr));
    setnonblocking(sock,0);
    int len=sendto(sock, (const char *)buffer,  8+strlen(msgstr), 0, (const sockaddr *)&servAddr, sizeof(servAddr));
    setnonblocking(sock,1);
    return  len;
}

int SendUdpAuth(int sock)
{
   // string str="{\"Type\":\"Auth\",\"Payload\":{\"Version\":\"2\",\"MmVersion\":\"1.7\",\"User\":\""+user+"\",\"Password\": \"\",\"OS\":\"darwin\",\"Arch\":\"amd64\",\"ClientId\":\""+ClientId+"\"}}";
    char str[255];
    memset(str,0,255);
    sprintf(str,"{\"Type\":\"Auth\",\"Payload\":{\"Version\":\"2\",\"MmVersion\":\"1.7\",\"User\":\"%s\",\"Password\": \"%s\",\"OS\":\"darwin\",\"Arch\":\"amd64\",\"ClientId\":\"%s\"}}",udpInfo.authtoken.c_str(),udpInfo.password_c.c_str(),udpInfo.ClientId.c_str());
    return SendUdpPack(sock,udpInfo.servAddr,str);
}

int SendUdpProxy(int sock,struct sockaddr_in servAddr,char* Data,char* Url,char* ClientAddr)
{
   // string str="{\"Type\":\"Auth\",\"Payload\":{\"Version\":\"2\",\"MmVersion\":\"1.7\",\"User\":\""+user+"\",\"Password\": \"\",\"OS\":\"darwin\",\"Arch\":\"amd64\",\"ClientId\":\""+ClientId+"\"}}";
    char str[255];
    memset(str,0,255);
    sprintf(str,"{\"Type\":\"UdpProxy\",\"Payload\":{\"Data\":\"%s\",\"Url\":\"%s\",\"ClientAddr\":\"%s\"}}",Data,Url,ClientAddr);
    return SendUdpPack(sock,servAddr,str);
}


int SendUdpPing(int sock)
{
    return SendUdpPack(sock,udpInfo.servAddr,"{\"Type\":\"Ping\",\"Payload\":{}}");

}


int SendUdpReqTunnel(int sock,TunnelInfo* tunnelinfo)
{
    if(strlen(tunnelinfo->ReqId)==0){
        char guid[20]={0};
        rand_str(guid,5);
        memcpy(tunnelinfo->ReqId,guid,strlen(guid));//copy
    }
    char str[1024];
    memset(str,0,1024);
    sprintf(str,"{\"Type\":\"ReqTunnel\",\"Payload\":{\"Protocol\":\"%s\",\"ReqId\":\"%s\",\"Hostname\": \"\",\"Subdomain\":\"\",\"HttpAuth\":\"\",\"RemotePort\":%d,\"authtoken\":\"%s\"}}",tunnelinfo->protocol,tunnelinfo->ReqId,tunnelinfo->remoteport,udpInfo.authtoken.c_str());
    return SendUdpPack(sock,udpInfo.servAddr,str);
}


/*���ping*/
int CheckUdpPing(int sock){
    if(udpInfo.pingtime+udpInfo.ping<getUnixTime()&&udpInfo.auth!=0)
    {
        SendUdpPing(sock);
        udpInfo.pingtime=getUnixTime();
    }
    //������,����60��
    if(udpInfo.pongtime!=0&&udpInfo.pongtime+60<getUnixTime()){
        udpInfo.auth=0;
        udpInfo.authtime=0;
        udpInfo.regTunnel=0;
        udpInfo.G_TunnelAddr.clear();
    }
    return 0;
}

int CheckUdpAuth(int sock){
    if(udpInfo.auth==0&&(udpInfo.authtime==0||udpInfo.authtime+5<getUnixTime())){
       SendUdpAuth(sock);
       udpInfo.authtime=getUnixTime();
       udpInfo.pongtime=0;
    }
    return 0;
}

int CheckRegTunnel(int sock){
    TunnelInfo *tunnelinfo;
    list<TunnelInfo*>::iterator listit;
    if(udpInfo.auth==1&&udpInfo.regTunnel==0){
        for ( listit = G_TunnelList.begin(); listit != G_TunnelList.end(); ++listit )
        {
            tunnelinfo =(TunnelInfo	*)*listit;
            if(stricmp(tunnelinfo->protocol,"udp")==0){
                SendUdpReqTunnel(sock,tunnelinfo);
            }
        }
        udpInfo.regTunnel=1;
    }
    return 0;
}

int UdpRecv(fd_set* readSet){
    sockaddr_in fromAddr;
    int addrLen = sizeof(struct  sockaddr_in);
    char buffer[65535] = {0};
    char srcdata[65535] = {0};
    memset( srcdata, 0, 65535 );
    memset( srcdata, 0, 65535 );
    if(FD_ISSET(udpInfo.msock,readSet)){

        memset(&fromAddr, 0, sizeof(struct  sockaddr_in));
        int strLen = recvfrom(udpInfo.msock, buffer, 65534, 0, (struct sockaddr *)&fromAddr, &addrLen);
        if(strlen>0&&strLen!=-1){

            printf("udp:%s\r\n",buffer+8);
            cJSON *json = cJSON_Parse( buffer+8);
             if(json)
             {
                cJSON *Type = cJSON_GetObjectItem( json, "Type" );
                if ( stricmp( Type->valuestring, "NewTunnel" ) == 0 )
                {
                    NewTunnel(json);
                }
                else if(stricmp( Type->valuestring, "AuthResp" ) == 0 )
                {
                    cJSON	*Payload	= cJSON_GetObjectItem( json, "Payload" );
                    char	*error		= cJSON_GetObjectItem( Payload, "Error" )->valuestring;
                    if(stricmp(error,"")==0)
                    {
                        udpInfo.auth=1;
                    }
                }
                else if(stricmp( Type->valuestring, "Pong" ) == 0 )
                {
                    udpInfo.pongtime = getUnixTime();
                }
                else if(stricmp( Type->valuestring, "UdpProxy" ) == 0 )
                {

                    cJSON	*Payload	= cJSON_GetObjectItem( json, "Payload" );
                    char	*Url		= cJSON_GetObjectItem( Payload, "Url" )->valuestring;
                    char	*Data		= cJSON_GetObjectItem( Payload, "Data" )->valuestring;
                    if(udpInfo.G_TunnelAddr.count(string(Url))>0)
                    {

                        //��������ͨ�Ŷ���
                        TunnelReq *tunnelreq= udpInfo.G_TunnelAddr[string(Url)];
                        struct sockaddr_in addr;
                        memset(&addr, 0, sizeof(struct  sockaddr_in));  //ÿ���ֽڶ���0���
                        addr.sin_family =AF_INET;
                        addr.sin_port =htons(tunnelreq->localport);
                        addr.sin_addr.s_addr = inet_addr(tunnelreq->localhost);
                        int len = 0;
                        memset(srcdata,0,65535);
                        base64_decode(Data, (int)strlen(Data), srcdata, &len);
                        setnonblocking(udpInfo.lsock,0);
                        sendto(udpInfo.lsock, (const char *)srcdata,  len, 0, (const sockaddr *)&addr, sizeof(struct  sockaddr_in));
                        setnonblocking(udpInfo.lsock,1);
                    }else{

                    }
                }

            }
        }
    }

    if(FD_ISSET(udpInfo.lsock,readSet)){

        memset(&fromAddr, 0, sizeof(struct  sockaddr_in));
        int strLen = recvfrom(udpInfo.lsock, buffer, 65534, 0,(struct sockaddr *) &fromAddr, &addrLen);
        memset(srcdata,0,65535);
        int len = 0;
        base64_encode(buffer, strLen, srcdata, &len);

        if(strLen>0){
            sockaddr_in sin;
            memset(&sin, 0, sizeof(sockaddr_in));
            memcpy(&sin, &fromAddr, sizeof(sin));
            char TmpUri[255]={0};
            if(GetUdpRemoteAddr(ntohs(fromAddr.sin_port),TmpUri)==0)
            {
                SendUdpProxy(udpInfo.msock,udpInfo.servAddr,srcdata,TmpUri,"");
            }
        }
    }

}

int initUdp(){
    //�����׽���
    udpInfo.msock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    //��������ַ��Ϣ
    memset(&udpInfo.servAddr, 0, sizeof(struct sockaddr_in));  //ÿ���ֽڶ���0���
    udpInfo.servAddr.sin_family = AF_INET;
    udpInfo.servAddr.sin_addr.s_addr = inet_addr(mainInfo.udphost);
    udpInfo.servAddr.sin_port = htons(mainInfo.udpport);
    //����socket����
    udpInfo.lsock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP );
    setnonblocking(udpInfo.msock,1);
    setnonblocking(udpInfo.lsock,1);
    return 0;
}

/*��ʵ��ʹ��,�����ο�*/
int UdpClient(){

    initUdp();
    fd_set readSet;
    struct timeval timeout;
    timeout.tv_sec=0;
    timeout.tv_usec=0;
    int SelectRcv;

    while(1){
        //login
        CheckUdpAuth(udpInfo.msock);
        //reg tunnel
        CheckRegTunnel(udpInfo.msock);
        CheckUdpPing(udpInfo.msock);//ping


        FD_ZERO(&readSet);//�������������һ����������
        FD_SET(udpInfo.msock,&readSet); //��sock����Ҫ���Ե���������
        FD_SET(udpInfo.lsock,&readSet); //��sock����Ҫ���Ե���������
        int maxfd=udpInfo.msock>udpInfo.lsock?udpInfo.msock:udpInfo.lsock;
        SelectRcv = select(maxfd+1,&readSet,0,0, &timeout); //�����׽����Ƿ�ɶ�

        if (SelectRcv > 0)
        {
            UdpRecv(&readSet);
        }
        sleeps(2);
    }
    closesocket(udpInfo.msock);
}
#endif