#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_SIZE 80
#define MAX_THREAD_SIZE 1000

//struct that's shared between threads
typedef struct dirThreadInfo 
{
  char srcDirPath[MAX_SIZE];
  char destDirPath[MAX_SIZE];
  int isRestoreOperation;  
} dir_thread_info;


//threadStats struct sent to parent thread
typedef struct threadStats 
{
  unsigned long totalBytesCopied;
  int filesCreated;
  int subDirectoriesCreated;  
} thread_stats;

//file thread that handles copying/restoring files only
void * file_thread(void * arg)
{
  FILE *src_file, *dest_file;
  thread_stats * threadStats = (thread_stats *)malloc(sizeof(thread_stats));
  threadStats->totalBytesCopied = 0;
  threadStats->filesCreated = 0;
  threadStats->subDirectoriesCreated = 0;
  
  char fileContents[1000];
  int srcRc, desRc;
  dir_thread_info * fileInfo = (dir_thread_info *)arg;
  
  struct stat srcBuf, desBuf;
  srcRc = lstat(fileInfo->srcDirPath, &srcBuf);
  if(srcRc == -1)
  {
    perror("stat() failed");
    exit(EXIT_FAILURE);
  }

  desRc = lstat(fileInfo->destDirPath, &desBuf);

  //fileInfo->isRestoreOperation is set to 1 if its a restore operation and 0 otherwise
  if(fileInfo->isRestoreOperation)
    printf("[thread %u] Restoring:%s\n", ((unsigned int)pthread_self()), fileInfo->srcDirPath ); 
  else
    printf("[thread %u] Backing up:%s\n", ((unsigned int)pthread_self()), fileInfo->srcDirPath );
  
  if(!fileInfo->isRestoreOperation)
  {
    if(desBuf.st_mtim.tv_sec >= srcBuf.st_mtim.tv_sec)//&& desBuf.st_mtim.tv_nsec >= srcBuf.st_mtim.tv_nsec)
    {
      
      threadStats->filesCreated = 0;
      printf("[thread %u] Backup copy of %s is already upto date\n", ((unsigned int)pthread_self()), fileInfo->srcDirPath );    
      pthread_exit(threadStats);
      exit(EXIT_SUCCESS);
    }
  }

  src_file = fopen(fileInfo->srcDirPath, "r");
  dest_file = fopen(fileInfo->destDirPath, "w");
  
  if (!src_file || !dest_file)
    exit(EXIT_FAILURE);

  if(!fileInfo->isRestoreOperation && desRc == 0)
    printf("[thread %u] WARNING: %s exists (overwriting!)\n", ((unsigned int)pthread_self()), fileInfo->srcDirPath );
  
  while(fgets(fileContents, 1000, src_file)!=NULL)
  {
    fprintf(dest_file, "%s", fileContents);  
  }
    
  printf("[thread %u] Copied %u bytes from %s to %s\n", ((unsigned int)pthread_self()), ((unsigned int)srcBuf.st_size) , fileInfo->srcDirPath, fileInfo->destDirPath );
  fclose(dest_file);
  fclose(src_file);
  free(arg);
  threadStats->totalBytesCopied = srcBuf.st_size;
  threadStats->subDirectoriesCreated = 0;
  threadStats->filesCreated = 1;
  
  //return threadStats from this thread to its parent
  pthread_exit(threadStats);
}

//function to check and create a  directory
void checkAndCreateDirectory(char dirPath[])
{
  int status = mkdir(dirPath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if(status >= 0)
    printf("[thread %u] Created %s\n", ((unsigned int)pthread_self()), dirPath );
}

//directory thread that handles copying/restoring files only
void * dir_thread( void * arg )
{
  dir_thread_info * dirInfo = (dir_thread_info *)arg;
  thread_stats * threadStats = (thread_stats *)malloc(sizeof(thread_stats));
  threadStats->totalBytesCopied = 0;
  threadStats->subDirectoriesCreated = 0;
  threadStats->filesCreated = 0;

  struct dirent * file;

  pthread_t threads[MAX_THREAD_SIZE];
  int threadCounter = 0;
  
  DIR * dir = opendir(dirInfo->srcDirPath);
  if ( dir == NULL )
  {
    perror( "opendir() failed" );
    exit(EXIT_FAILURE);
  }
  
  checkAndCreateDirectory(dirInfo->destDirPath); 
  
  while ( ( file = readdir( dir ) ) != NULL )
  {
    struct stat buf;
    
    //Exclude special files like my .mybackup, backupUtil.c, etc
    if(strcmp(file->d_name, ".") && strcmp(file->d_name, "..") && strcmp(file->d_name, ".mybackup") && strcmp(file->d_name, "backupUtil.c") && strcmp(file->d_name, "myBackUpUtil"))
    {
      char tempPath[MAX_SIZE];
      strcpy(tempPath, dirInfo->srcDirPath);
      strcat(tempPath, "/");
      strcat(tempPath, file->d_name);
      
      int rc = lstat( tempPath, &buf ); 

      if ( rc == -1 )
      {
        perror( "stat() failed" );
        exit(EXIT_FAILURE);
      }

      //Its a directory. Call dir_thread method to handle directory creation 
      if ( S_ISDIR( buf.st_mode ) )
      {
        dir_thread_info * newDirInfo = (dir_thread_info *)malloc(sizeof(dir_thread_info));
        strcpy(newDirInfo->srcDirPath, tempPath);
            
        strcpy(newDirInfo->destDirPath, dirInfo->destDirPath);
        strcat(newDirInfo->destDirPath, "/");
        strcat(newDirInfo->destDirPath, file->d_name);        
        
        if(dirInfo->isRestoreOperation)
        {
          newDirInfo->isRestoreOperation = 1;
        }
        else
        {
          newDirInfo->isRestoreOperation = 0;
        }

        rc = pthread_create( &threads[threadCounter++], NULL, dir_thread, newDirInfo );
        threadStats->subDirectoriesCreated++;
      }
      else if(S_ISREG( buf.st_mode) )
      {
        //Just a regular file. Call file_thread method to handle file creation
        dir_thread_info * newFileInfo = (dir_thread_info *)malloc(sizeof(dir_thread_info));
        strcpy(newFileInfo->srcDirPath, tempPath);
        
        strcpy(newFileInfo->destDirPath, dirInfo->destDirPath);
        strcat(newFileInfo->destDirPath, "/");
        strcat(newFileInfo->destDirPath, file->d_name);        
        
        if(dirInfo->isRestoreOperation)
        {
          (newFileInfo->destDirPath)[strlen(newFileInfo->destDirPath) - 4] = '\0';
          newFileInfo->isRestoreOperation = 1;
        }
        else
        {
          strcat(newFileInfo->destDirPath, ".bak");   
          newFileInfo->isRestoreOperation = 0;
        }
        
        rc = pthread_create( &threads[threadCounter++], NULL, file_thread, newFileInfo );
        //threadStats->filesCreated++;
      }
    }
  }
  
  int i;
  thread_stats * threadsCreatedStats;
  //Wait for all threads created here to join with this thread
  for(i = 0; i < threadCounter; i++)
  {
    pthread_join(threads[i], (void **)&threadsCreatedStats);
    threadStats->totalBytesCopied += threadsCreatedStats->totalBytesCopied;
    threadStats->subDirectoriesCreated += threadsCreatedStats->subDirectoriesCreated;
    threadStats->filesCreated += threadsCreatedStats->filesCreated;
  }
  free(arg);

  pthread_exit(threadStats);  
}


int main(int argc, char *argv[]) 
{
  pthread_t dirThread;

  dir_thread_info * dirInfo = (dir_thread_info *)malloc(sizeof(dir_thread_info));
  
  //Handle populating dirInfo based on the the option provided by user when running this code
  if(argc == 2 && !strcmp(argv[1], "-r"))
  {
      strcpy(dirInfo->destDirPath, ".");
      strcpy(dirInfo->srcDirPath, "./.mybackup");
      dirInfo->isRestoreOperation = 1;
  }
  else
  {
    strcpy(dirInfo->srcDirPath, ".");
    strcpy(dirInfo->destDirPath, "./.mybackup");
    dirInfo->isRestoreOperation = 0;
  }
  
  thread_stats * retVal;
  int rc = pthread_create( &dirThread, NULL, dir_thread, dirInfo );
  if ( rc == -1 )
  {
    perror( "stat() failed" );
    return EXIT_FAILURE;
  }
  rc = pthread_join(dirThread, (void **)&retVal);
  if(retVal)
  {
    if(retVal->subDirectoriesCreated == 0)
      printf("Successfully backed up %d files (%lu bytes)\n", retVal->filesCreated, retVal->totalBytesCopied );
    else
      printf("Successfully backed up %d files (%lu bytes) and %d sub-directories\n", retVal->filesCreated, retVal->totalBytesCopied, retVal->subDirectoriesCreated );
  }
  return EXIT_SUCCESS;
}