#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "debug.h" 
#include "history.h"
#include "timer.h"
#include "tokenizer.h"
#include <signal.h>
#include <ctype.h>


//struct for storing pid and the command
struct Jobs 
{ 
int pid;
char *command;
}; 

struct Jobs arr[10] = {0};
// boolean that checks if the command has end with a '&'
bool is_background_command=false;
// boolean that checks if the command was NULL or not
bool is_command=true;
int command_num=0;
int token_counter=0;
//history is a 2D array becuase it stores an array of strings
char history[1000][1000];
//keeps track of history index
int h_index=0;
//keeps track of jobs struct index
int job_index=0;

int  getcommands(char** tokens);
void execute(char** tokens);
void print_prompt(void);
// SIGCHLD handler: retrieves a pid, then checks if that pid matches any of
// the pid's in jobs struct, if it does then I delte both the entries at that index
void sigchild_handler(int signo)
{
	pid_t pid=waitpid(-1, &signo, WNOHANG);
	for(int i=9; i>=0; i--)
	{
		if(arr[i].command!=NULL && arr[i].pid==pid)
		{
			arr[i].pid='0';
			free(arr[i].command);
			arr[i].command= NULL;
		}
	}

}
// SIGINT handler: Checks if after Ctrl C is entered if the command was a blank command 
// also checks if in scripting mode if in either one the prompt is printed
void sigint_handler(int signo) 
{
    printf("\n");
    if(is_command==false)
    {
	print_prompt();
    }
    if (isatty(STDIN_FILENO))
    {
	print_prompt();

    }
    fflush(stdout);

}
// prints prompt on the command line
void print_prompt(void)
{
	int i;
	char hostname[1024];
	gethostname(hostname, sizeof(hostname));
	char* username = getenv("USER");
	char cwd[1024];
        getcwd(cwd, sizeof(cwd));	

	for(i=strlen(cwd);i>0;i--){
		if(cwd[i]=='/'){
			break;
		}

	}
	int starting_index= i-1;
	cwd[starting_index]='~';	
	printf("[%d--%s@%s-- %s]$ ",command_num, username, hostname, cwd+starting_index );
	fflush(stdout);

}
// function for built in's
// command number is incremented for all builtin's except exit
int bulitin(char** tokens)
 {
	int history_index;
	int j_index;
	//exit's program when user enters "exit"		
	if(strcmp(tokens[0], "exit")==0)
	{
		exit(0);
	}
	// if the user only enters "cd" the current directory is now the home directory
	if( strcmp(tokens[0],"cd")==0 && tokens[1]==NULL)
	{
			
		command_num++;
		chdir(getenv("HOME"));
		return 1;
	}
	// if the user enters cd, the cd function is executed on the second token
	if(strcmp(tokens[0], "cd")==0)
	{
		chdir(tokens[1]);
		command_num++;
		return 1;
			
	}
	//if the user enters history, the history list will be printed out 
	if(strcmp(tokens[0],"history")==0)
	{
		
		command_num++;
		//loops through history array and if the command number is less than 100
		//it simply prints the all the elements in the array
		for(history_index=0; history_index< command_num; history_index++)
		{


			if(command_num<100)
			{
				
				printf("%d %s\n",history_index, history[history_index]);
			}
		}
		//if the command number is greater than 100 prints the entire array but 
		//starts at at 100 behind the command number to only print the last 100 
		//commands
		if(command_num>100)
		{
			for(int i=command_num-100; i< command_num; i++)
			{
				printf("%d %s\n", i, history[i]);
			}


		}
		return 1;
	}
	//if the user enters setenv, the second and third token will be the pararmeters 
	if(strcmp(tokens[0], "setenv")==0)
	{			
		//third parameter any nozero to override 
	
		command_num++;
		setenv(tokens[1],tokens[2],1);					
		return 1;
	}
	//if the user enters jobs, the pid and command of any background command will be printed
	if(strcmp(tokens[0], "jobs")==0)
	{
		command_num++;
		for(j_index=0; j_index< 10; j_index++)
		{
			if(arr[j_index].command != NULL)
			{
				
			
				printf("[%d]	 %s\n",arr[j_index].pid, arr[j_index].command);
			}
		}

		return 1;
	}
 	return 0;
 }
//struct for redirection
struct command_line 
{
    char **tokens;
    bool stdout_pipe;
    char *stdout_file;
};

int execute_pipeline(struct command_line *cmds)
{
	//base case
	if (cmds[0].stdout_pipe== false)
   	{		
        	if(cmds[0].stdout_file !=NULL)
        	{

            		int fdout = open(cmds[0].stdout_file, O_WRONLY);
            		dup2(fdout, STDOUT_FILENO);
        	}
        	return	execvp(cmds[0].tokens[0], cmds[0].tokens);
        	

    	}
	int fd[2];
    	if(pipe(fd)==-1)
    	{
        	perror("pipe");
        	return -1 ;
    	}
    
	pid_t pid = fork();
    	if (pid==0)
    	{
        //child
        	dup2(fd[1],STDOUT_FILENO);
        	close(fd[0]);
        	return execvp(cmds[0].tokens[0], cmds[0].tokens);

    	}

    	else
    	{

        	dup2(fd[0], STDIN_FILENO);
        	close(fd[1]);
        	return execute_pipeline(cmds+1);

    	}

}
//function that executes basic linux commands
void execute(char** tokens)
{
	
	pid_t pid = fork();

	if(pid==0)
	{//child

		int ret =getcommands(tokens);
		if(ret<0)
		{
			perror("Error");
			fclose(stdin);
			exit(0);
		}

	}
	else if(pid == -1) 
	{
		perror("fork");	
	}
	else
	{//parent
		command_num++;
		int status;
		//when a '&' was found at the end of a command
		if(is_background_command==true)
		{
			//set boolean back to default
			is_background_command= false;
			fflush(stdout);
			arr[job_index++].pid= pid;
			waitpid(pid, &status, WNOHANG);

		}

		else
		{
		//if it is not a background command wait for command to terminate before
		//prompting for the next command
			waitpid(pid, &status, 0);
		}
	}

}
//function that turns tokens into a struct that executes execute_pipeline
int getcommands(char** tokens)
{
	
	struct command_line cmds[1000] = {0};
	int struct_index=0;
	int index_saver=0;
	cmds[struct_index].tokens = malloc(sizeof(char *) * token_counter);
	cmds[struct_index].stdout_file=NULL;
	for(int i=0; i<token_counter;i++)
	{
	
		if(strcmp(tokens[i], "|")==0)
		{
			cmds[struct_index].stdout_pipe=true;
			cmds[struct_index].stdout_file=NULL;
			struct_index++;
			index_saver=0;	
			cmds[struct_index].tokens = malloc(sizeof(char *) * token_counter);
			cmds[struct_index].stdout_file=NULL;
		} 
		else if(strcmp(tokens[i], ">")==0)
		{
			cmds[struct_index].stdout_pipe=false;
			for(i=i+1;i<token_counter;i++)
			{
				cmds[struct_index].tokens[i]=tokens[i];
			}
		}
		else 
		{
			
			cmds[struct_index].tokens[index_saver++]=tokens[i];
		}
	}
	return execute_pipeline(&cmds[0]);


	return 1;
}
int main(void) 
{
  //keeps track of number to re run during !#
  int re_run;
  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, sigchild_handler);
   //infinite loop program constantly runs until exit is entered
   while (true) 
   {
	 
	 // shell scripting mode 
	 if(isatty(STDIN_FILENO))
	 {
		 print_prompt();
	 }
   
        char *line = NULL;
        size_t line_sz = 0;
        //retrieves line that was entered in the command line
	getline(&line, &line_sz, stdin);
	
	if(*line==-1)
	{
	  perror("Error");
	}	
        
	
	char *tokens[1000] = { 0 };	
        tokens[0]=line;
	int end_of_line = strlen(line)-2;
	//only copies the line into history if a blank command was not entered
	if(line[0]!='\n')
	{
		strcpy(history[h_index++], line);
	}
	//if the last character of the line just entered is equal to the ASCII value
	//of '&' and a blank command was not entered
	//then i set my boolean to true to know that a background command has been entered
	
	if(line[end_of_line]==38 && line[0]!='\n')
	{
		is_background_command = true;
		line[end_of_line]='\n';
		arr[job_index].command = strdup(line);
	}
	else
	{
		
		is_background_command = false;
	}

        int i =0;
	char *next_tok = line;
	char *curr_tok=	next_token(&next_tok, " \t\n");
	
	//if the current token is NULL or a blank command, my global boolean determines 
	//if print_prompt needs to be printed during a SIGINT command
	if(curr_tok==NULL)
	{
		//checks if a command was entered
		is_command=false;
		continue;
	}
	while (curr_tok  != NULL)  
	{
		is_command=true;
		//adds current token into tokens array
		tokens[i++]= curr_tok;
		token_counter++;
		if(curr_tok[0]=='#')
		{
			if(i==1)
			{		
				command_num++;
			}	
			tokens[i-1]= NULL;
		}
		if(curr_tok[0]=='$')

		{
			char* retrieved =getenv(curr_tok+1);
			tokens[i-1]=retrieved;
		}
		if(curr_tok[0]=='!'&& isdigit(curr_tok[1]) !=0)
		{
			if(command_num < 100)
			{
				re_run=atoi(curr_tok +1);
			//	printf("rerun: %d\n", re_run);
			}

			else
			{
				re_run=atoi(curr_tok + 1)-(command_num-1-100);
			//	printf("rerun: %d\n", re_run);
			}	
			char* rerun_copy=strdup(history[re_run]);
			strcpy(history[h_index-1], rerun_copy);
			curr_tok=(next_token(&rerun_copy, " \n\t"));
			tokens[0]=curr_tok;
			if(rerun_copy[0]=='#')
			{
				printf("%s", rerun_copy);
			}
			i=1;
		//	free(rerun_copy);			
		}
		
		if(curr_tok[0]=='!' && curr_tok[1]=='!')
		{
			char* history_copy=strdup(history[command_num-1]);
			strcpy(history[h_index-1], history_copy);
			curr_tok= next_token(&history_copy, " \n\t");
		//	printf("tokens[0]: %s\n", tokens[0]);
			tokens[0]=curr_tok;
			
		//	printf("history_copy: %s curr_tok[0]: %d tokens[0]: %s\n", history_copy, curr_tok[0], tokens[0]);
			if((char )curr_tok[0]=='#')
			{
				printf("%s %s", curr_tok, history_copy);
				free(history_copy);
			}
			
			i=1;
		//	free(history_copy);
		}
		
		int h_indexx;
		for(h_indexx=h_index;h_indexx>0;h_indexx--)
		{
			if(curr_tok[0]=='!' && curr_tok[1]==history[h_indexx][0])
			{
				char *prefix_copy=strdup(history[h_indexx]);
				strcpy(history[h_index-1], prefix_copy);
				curr_tok= next_token(&prefix_copy, " \n\t");
				tokens[0]=curr_tok;
				if(curr_tok[0]=='#')
				{
					printf("%s %s", curr_tok, prefix_copy);
					free(prefix_copy);
				}
				i=1;
			//	free(prefix_copy);
			}	
		

		}

		curr_tok=next_token(&next_tok, " \t\n");
	}


	
	tokens[i] =(char *) NULL;
	if (tokens[0] == NULL) {
		continue;

	}
	if(bulitin(tokens)!=1)
	{
		execute(tokens);

	}
	
	token_counter=0;
    
  	  }
   	//free(rerun_copy);
	//free(history_	
   	return 0;

	}
