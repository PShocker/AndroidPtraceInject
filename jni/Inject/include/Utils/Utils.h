// system lib
#include <asm/ptrace.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <dirent.h>
#include <elf.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/unistd.h>
#include <sys/stat.h>
#include <sys/system_properties.h>

// 系统lib路径
struct process_libs{
    const char *libc_path;
    const char *linker_path;
    const char *libdl_path;
} process_libs = {"","",""};

/**
 * @brief 处理各架构预定义的库文件
 */
__unused __attribute__((constructor)) void handle_libs(){ // __attribute__((constructor))修饰 最先执行
    char sdk_ver[32];
    __system_property_get("ro.build.version.sdk", sdk_ver);
// 系统lib路径
#if defined(__aarch64__) || defined(__x86_64__)
    // 在安卓11(包含安卓11)以上 lib路径有所变动
    if ( atoi(sdk_ver) >=  __ANDROID_API_R__){
        process_libs.libc_path = "/apex/com.android.runtime/lib64/bionic/libc.so";
        process_libs.linker_path = "/apex/com.android.runtime/bin/linker64";
        process_libs.libdl_path = "/apex/com.android.runtime/lib64/bionic/libdl.so";
    } else {
        process_libs.libc_path = "/system/lib64/libc.so";
        process_libs.linker_path = "/system/bin/linker64";
        process_libs.libdl_path = "/system/lib64/libdl.so";
    }
#else
    process_libs.libc_path = "/system/lib/libc.so";
    process_libs.linker_path = "/system/bin/linker";
    process_libs.libdl_path = "/system/lib/libdl.so";
#endif
    printf("[+] libc_path is %s\n", process_libs.libc_path);
    printf("[+] linker_path is %s\n", process_libs.linker_path);
    printf("[+] libdl_path is %s\n", process_libs.libdl_path);
    printf("[+] system libs is OK\n");
}

/**
 * @brief 执行shell并且返回shell执行内容
 * @param cmd 执行到shel内容
 * @param result shell到返回结果
 * @return 是否执行成功
 */
int exec_cmd(const char *cmd, char *result){
    FILE *pipe = popen(cmd, "r");
    if(!pipe){
        return 0;
    }
    char buffer[128] = {0};
    while(!feof(pipe)){
        if(fgets(buffer, 128, pipe)){
            strcat(result, buffer);
        }
    }
    pclose(pipe);
    return 1;
}

/**
 * @brief Get the pid by pkg_name
 * 成功返回 true
 * 失败返回 false
 * 
 * @param pid 
 * @param task_name 
 * @return true 
 * @return false 
 */
bool get_pid_by_name(pid_t *pid, char *task_name){
    DIR *dir;
    struct dirent *ptr;
    FILE *fp;
    char filepath[50];
    char cur_task_name[50];
    char buf[1024];

    dir = opendir("/proc");
    if (NULL != dir){
        while ((ptr = readdir(dir)) != NULL){ //循环读取/proc下的每一个文件/文件夹
            //如果读取到的是"."或者".."则跳过，读取到的不是文件夹名字也跳过
            if ((strcmp(ptr->d_name, ".") == 0) || (strcmp(ptr->d_name, "..") == 0))
                continue;
            if (DT_DIR != ptr->d_type)
                continue;

            sprintf(filepath, "/proc/%s/cmdline", ptr->d_name); //生成要读取的文件的路径
            fp = fopen(filepath, "r");
            if (NULL != fp){
                if (fgets(buf, 1024 - 1, fp) == NULL){
                    fclose(fp);
                    continue;
                }
                sscanf(buf, "%s", cur_task_name);
                //如果文件内容满足要求则打印路径的名字（即进程的PID）
                if (strstr(task_name, cur_task_name)){
                    *pid = atoi(ptr->d_name);
                    return true;
                }
                fclose(fp);
            }
        }
        closedir(dir);
    }

    return false;
}

/**
 * @brief Get the app start activity object 
 * 获取应用的android.intent.action.MAIN
 *
 * @param pkg_name // 获取的应用包名
 * @param start_activity_name // 存放启动的 main activity
 */
void get_app_start_activity(char *pkg_name, char *start_activity_name){
    char cmdstring[1024] = "dumpsys package ";
    char cmd_string[1024] = {0};
    char temp_file[] = "tmp_XXXXXX";

    strcat(cmdstring, pkg_name);
    int fd;

    if ((fd = mkstemp(temp_file)) == -1){
        printf("[-] create tmp file failed.\n");
    }

    sprintf(cmd_string, "%s > %s", cmdstring, temp_file);
    system(cmd_string);

    FILE *fp = fdopen(fd, "r");
    if (fp == NULL){
        printf("[-] can not load file!");
        return;
    }
    char line[1024];
    while (!feof(fp)){
        fgets(line, 1024, fp);
        if (strstr(line, "android.intent.action.MAIN")){
            fgets(line, 1024, fp);
            char *p;
            //选取第二个
            int index = 1;
            p = strtok(line, " ");
            while (p){
                if (index == 2){
                    strcpy(start_activity_name, p);
                }
                index++;
                p = strtok(NULL, " ");
            }
            break;
        }
    }
    fclose(fp);
    unlink(temp_file);
    return;
}

/**
 * @brief 输入App包名 利用 am 命令 启动App
 * 
 * @param pkg_name 
 */
void start_app(char *pkg_name){
    char start_activity_name[1024] = {0};
    get_app_start_activity(pkg_name, start_activity_name);
    printf("[+] app_start_activity is %s\n", start_activity_name);
    char start_cmd[1024] = "am start ";
    strcat(start_cmd, start_activity_name);
    printf("[+] %s\n", start_cmd);
    system(start_cmd);
}

/**
 * @brief 在指定进程中搜索对应模块的基址
 *
 * @param pid pid表示远程进程的ID 若为-1表示自身进程
 * @param ModuleName ModuleName表示要搜索的模块的名称
 * @return void* 返回0表示获取模块基址失败，返回非0为要搜索的模块基址
 */
void *get_module_base_addr(pid_t pid, const char *ModuleName){
    FILE *fp = NULL;
    long ModuleBaseAddr = 0;
    char szFileName[50] = {0};
    char szMapFileLine[1024] = {0};

    // 读取"/proc/pid/maps"可以获得该进程加载的模块
    if (pid < 0){
        //  枚举自身进程模块
        snprintf(szFileName, sizeof(szFileName), "/proc/self/maps");
    } else {
        snprintf(szFileName, sizeof(szFileName), "/proc/%d/maps", pid);
    }

    fp = fopen(szFileName, "r");

    if (fp != NULL){
        while (fgets(szMapFileLine, sizeof(szMapFileLine), fp)){
            if (strstr(szMapFileLine, ModuleName)){
                char *Addr = strtok(szMapFileLine, "-");
                ModuleBaseAddr = strtoul(Addr, NULL, 16);

                if (ModuleBaseAddr == 0x8000)
                    ModuleBaseAddr = 0;

                break;
            }
        }

        fclose(fp);
    }

    return (void *)ModuleBaseAddr;
}

/**
 * @brief 获取远程进程与本进程都加载的模块中函数的地址
 *
 * @param pid pid表示远程进程的ID
 * @param ModuleName ModuleName表示模块名称
 * @param LocalFuncAddr LocalFuncAddr表示本地进程中该函数的地址
 * @return void* 返回远程进程中对应函数的地址
 */
void *get_remote_func_addr(pid_t pid, const char *ModuleName, void *LocalFuncAddr){
    void *LocalModuleAddr, *RemoteModuleAddr, *RemoteFuncAddr;
    //获取本地某个模块的起始地址
    LocalModuleAddr = get_module_base_addr(-1, ModuleName);
    //获取远程pid的某个模块的起始地址
    RemoteModuleAddr = get_module_base_addr(pid, ModuleName);
    // local_addr - local_handle的值为指定函数(如mmap)在该模块中的偏移量，然后再加上remote_handle，结果就为指定函数在目标进程的虚拟地址
    RemoteFuncAddr = (void *)((uintptr_t)LocalFuncAddr - (uintptr_t)LocalModuleAddr + (uintptr_t)RemoteModuleAddr);

    printf("[+] [get_remote_func_addr] lmod=0x%lX, rmod=0x%lX, lfunc=0x%lX, rfunc=0x%lX\n", LocalModuleAddr, RemoteModuleAddr, LocalFuncAddr, RemoteFuncAddr);
    return RemoteFuncAddr;
}

