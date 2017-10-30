#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <string>
#include <map>

const int MAX_BUFFER_LEN = 1024;
const int MAX_PATH_LEN = 256;
const char *G_FILE_PATH_UPDTE_TMP = "/tmp/updatetmp";

using namespace std;

map<string, string> g_tar_map;
long int g_nTotalFileSize = 0;
long int g_nCurrentFileSize = 0;

int g_nWorkProcess = -1;

void sig_handler(int sig)
{
    switch (sig)
    {
        //收到中断信号就代表异常退出，退出码-1
        case SIGTERM:
        case SIGABRT:
        case SIGQUIT:
        case SIGINT:
        {
            exit(-1);
        }
            break;
        default:
            break;
    }
}

int get_system_command_exec_status(int status)
{
    int nRet = 0;
    if (-1 == status)
    {
        printf("system error!");
        return -1;
    }
    else
    {
        if (WIFEXITED(status)) //正常退出
        {
            if (0 == WEXITSTATUS(status))
            {
                //printf("run shell script successfully.\n");
                nRet = 0;
            }
            else
            {
                printf("run shell script fail, script exit code: %d\n", WEXITSTATUS(status));
                nRet = WEXITSTATUS(status);
            }

        }
        else //异常退出
        {
            printf("run script exit informal");
            nRet = -1;
        }
    }
    return nRet;
}

void Usage(char *cmdName)
{
    printf("Usage\n");
    printf("%s tarfile targetPath\n", cmdName);
    printf("\n");
}

int myTar()
{
    if (g_tar_map.empty())
    {
        printf("tar map empty\n");
        return 0;
    }

    //升级包压缩比例为1：1
   //1.先获取升级包大小
   char cmdBuf[MAX_PATH_LEN] = {0};
   char buf[MAX_BUFFER_LEN];
   memset(buf, 0, MAX_BUFFER_LEN);

   auto it = g_tar_map.begin();
   for( ; it != g_tar_map.end(); ++it)
   {
       snprintf(cmdBuf, MAX_PATH_LEN - 1,
           "ls -l %s | awk '{print $5}'", it->first.c_str());
       FILE *pf = popen(cmdBuf, "r");
       fread(buf, MAX_BUFFER_LEN, 1, pf);
       pclose(pf);
       g_nTotalFileSize += atoi(buf);
       printf("get file size = %s\n", buf);
   }

    int   fd[2];
    int nStatus = 0;
    pid_t pid;
    if (pipe(fd) < 0)
      return -1;
    if ((pid = fork()) < 0)
      return -1;
    else if (pid > 0) //父进程
    {
      int nRead = 0;
      close(fd[1]);
      char tmpBuf[MAX_PATH_LEN] = {0};
      int nFd = open(G_FILE_PATH_UPDTE_TMP, O_RDWR|O_CREAT|O_TRUNC, 0666);
      while ((nRead = read(fd[0], buf, MAX_BUFFER_LEN -1)) > 0)
      {
          //printf("buf = %s\n", buf);
          char *token=strtok(buf, " "); //以空格分隔
          for ( int index=0; token != NULL; ++index)
          {
              if (token != NULL)
              {
                  //printf("stroke = %s\n", token);
                  if (2 == index)  //index 2代表当前解压大小
                  {
                      //printf("current file size = %d\n", atoi(token));
                      g_nCurrentFileSize += atoi(token);
                  }
              }
              token = strtok(NULL, " ");
          }

          //printf("percent %f%%\n\n", g_nCurrentFileSize*100.0 / g_nTotalFileSize);

          snprintf(tmpBuf, MAX_PATH_LEN - 1, "%f", g_nCurrentFileSize*100.0 / g_nTotalFileSize);

          lseek(nFd, SEEK_SET, 0); //调到文件头
          write(nFd, tmpBuf, strlen(tmpBuf));
          fsync(nFd); //同步写到文件中
      }

      close(nFd);
      close(fd[0]);

      waitpid(pid, &nStatus, 0);
      printf("wait for pipe close end\n");
      nStatus = get_system_command_exec_status(nStatus);
    }
    else //子进程
    {
        g_nWorkProcess = getpid();
        int nRet = 0;
        close(fd[0]);
        char cmdBuf[MAX_PATH_LEN] = {0};
        /*
         * @brief 将tar输出重定向到管道输入端
         **/
        auto it = g_tar_map.begin();
        for ( ; it != g_tar_map.end(); ++it)
        {
            snprintf(cmdBuf, MAX_PATH_LEN - 1,
                "tar -xvvf %s -C %s >&%d",
                it->first.c_str(),
                it->second.c_str(),
                fd[1]);
            printf("execl cmd[%s]\n", cmdBuf);
            int nSysRet = system(cmdBuf);
            nRet = get_system_command_exec_status(nSysRet);
            if (nRet != 0)
            {
                //解包出错，直接退出子进程
                printf("execl cmd[%s] exit informal.ret[%d]\n", cmdBuf, nSysRet);

                exit(-1);
            }
            system("sync");  //刷新到磁盘，再进行下一次数据解包
            usleep(500 * 1000);
        }
        return 0;; //解析完正常退出
    }

    printf("myTart end, ret[%d]\n\n", nStatus);
    return nStatus;
}


int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        Usage(argv[0]);
        return 0;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGABRT, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);

    string strCur;
    string strPrev;
    string strNext;
    for(int i = 0; i < argc; ++i)
    {
        /*
         * @brief 参数列表以'-o'分隔， '-o'前一个为源路径，后一个为目标路径
         **/
        if ( 0 == strncmp(argv[i], "-o", strlen("-o")) )
        {
            //前后有连续两个-o
            if (0 == strncmp(argv[i-1], "-o", strlen("-o"))
            || (argc > (i+1)
                && 0 == strncmp(argv[i+1], "-o", strlen("-o"))))
            {
                printf("two '-o' parameters\n");
                g_tar_map.clear(); //参数错误，直接清空map，拒绝运行
                return 0;
            }

            //解析
            if (i+1 == argc)  //-o为最后一个参数
            {
                printf("last parameter can not be '-o'\n");
                g_tar_map.clear(); //参数错误，直接清空map，拒绝运行
                return 0;
            }

            g_tar_map.insert(
                std::make_pair(argv[i-1],
                        argv[i+1]));

        }

    }

    int nPid = fork();
    int nStatus = 0;
    //子进程执行动作，父进程等待子进程结束
    if (nPid == 0)
    {
        int nRet = myTar();
        exit(nRet);
    }
    else //主进程
    {
        waitpid(nPid, &nStatus, 0);
        printf("main pricess wait end\n");
        int nRet = get_system_command_exec_status(nStatus);
        printf("TarTool Exit with code[%d]\n\n\n", nRet);
        return nRet;
    }
}
