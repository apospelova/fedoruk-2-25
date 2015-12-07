#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>

/* MACROS AND CONSTANTS */
#define IN_BUF_SIZE 1024
#define NUM_THREADS 2

#define ERROR_IF(cond) if(cond) { perror(#cond); exit(EXIT_FAILURE); } 
#define ERROR_IF_NULL(cond) ERROR_IF((cond) == NULL)
#define ERROR_IF_NON_ZERO(cond) ERROR_IF((cond)!= 0)

/* TYPE DEFINITIIONS */

typedef enum Mbox_status
{
	EMPTY,
	FULL
}mbox_status;

typedef enum State
{
	CHOOSE_THREAD,
	CHANGE_MODE,
	NORMAL
}state;

typedef enum Mode
{
	TRANSLATE,
	REVERSE,
	SWAP,
	KOI8,
	TO_UPPER,
	TO_LOWER,
	INVERT_CASE,
	MODE_COUNT
}mode;

typedef void (*mod_fun)(char *);

typedef struct {
	char mailbox [IN_BUF_SIZE];
	mbox_status mailbox_status;
	pthread_mutex_t	lock;
	pthread_cond_t cond;
	mod_fun mode_fun;
    mode m;
}thread_info;



/* FUNCTION PROTOTYPES */
void send_data(const char *buf, thread_info *info);
void receive_data(char *buf, thread_info *info);

void* thread1(void* arg);
void* thread2(void* arg);

int mt_printf(const char *format, ...);
void sigint_handler(int arg);

void translate(char *str);
void reverse(char *str);
void swap(char *str);
void koi8(char *str);
void to_upper(char *str);
void to_lower(char *str);
void invert_case(char *str);

/* GLOBAL VARIABLES */
thread_info thread_infos[NUM_THREADS];                   // массив описателей потоков
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER; // статическая инициализация глобального мьютекса - используется для функции mt_printf
void* (*thread_funs[])(void *arg) = {thread1, thread2};  // массив указателей на функции потоков
const char* mode_names[] = {"Translate", "Reverse", "Swap", "KOI8", "To upper case", "To lower case", "Invert case"}; // Текстовое описание режимов преобразования строк
mod_fun mode_functions[] = {translate, reverse, swap, koi8, to_upper, to_lower, invert_case}; // массив указателей на функции преобразования строк
state st = NORMAL; // Режим ввода: управление потоками/ввод текста



int main()
{
	char in_buf[IN_BUF_SIZE];
	int i;
	pthread_t tinfo[NUM_THREADS];
	
	/* инициализация программы */
	
	ERROR_IF( signal(SIGINT, sigint_handler) == SIG_ERR ); // установка обработчика Ctrl+C
	
	mt_printf("Type string and press enter\n");
	
	for(i=0; i<NUM_THREADS; i++)
	{
		thread_info *info;
		info = &thread_infos[i];
		info->mailbox_status = EMPTY;
		info->mode_fun = translate;
		info->m = TRANSLATE;
		ERROR_IF_NON_ZERO( pthread_mutex_init(&info->lock, NULL) );
		ERROR_IF_NON_ZERO( pthread_cond_init(&info->cond, NULL) );
		pthread_create(&tinfo[i], NULL, thread_funs[i], info );
	}
	
	thread_info *next_thread = &thread_infos[0];
	
	/* основная программа */
	while(1)
	{	
		unsigned int thread_no;
		unsigned int mode;
		
		ERROR_IF_NULL( fgets(in_buf, IN_BUF_SIZE, stdin) );
		

		if(st == CHOOSE_THREAD) // режим изменения режима работы потока
		{
			if( sscanf(in_buf, "%u", &thread_no) > 0 )
			{
				if(1 <= thread_no && thread_no <= NUM_THREADS)
				{
					mt_printf("Choose mode for thread %i: [%s]\n", thread_no, mode_names[thread_infos[thread_no-1].m]);
					for(i=0; i<MODE_COUNT; i++)
					{
						mt_printf("%i) %s\n", i, mode_names[i]);
					}
					st = CHANGE_MODE;
				}
				else
				{
					mt_printf("Invalid thread number %i\n"
					          "Choose thread number 1-%i:\n", thread_no, NUM_THREADS);
				}
			}
			else
			{
				mt_printf("Choose thread number 1-%i:\n", NUM_THREADS);
			}
		}else if(st == CHANGE_MODE) // режим изменения режима работы потока
		{
			if( sscanf(in_buf, "%u", &mode) > 0 )
			{
				thread_infos[thread_no-1].m = mode;
				thread_infos[thread_no-1].mode_fun = mode_functions[mode];
			}
			st = NORMAL;
			mt_printf("Type string and press enter\n");
		}
		else if(st == NORMAL) // режим ввода строк
		{
			
			in_buf[strcspn(in_buf, "\n")] = 0;
			mt_printf("[thread 0] \"%s\"\n", in_buf);
			send_data(in_buf, next_thread);
		}
	}
	
	return 0;
}

void send_data(const char *buf, thread_info *info)
{
	ERROR_IF_NON_ZERO( pthread_mutex_lock(&info->lock) );
	strcpy( info->mailbox, buf);
	info->mailbox_status = FULL;
	ERROR_IF_NON_ZERO( pthread_mutex_unlock(&info->lock) );
	ERROR_IF_NON_ZERO( pthread_cond_signal(&info->cond) ); // посылаем сигнал принимающему данные потоку, что он может начать обработку строки
}

void receive_data(char *buf, thread_info *info)
{
	ERROR_IF_NON_ZERO( pthread_mutex_lock(&info->lock) );
	while(info->mailbox_status == EMPTY) // проверка нужна потому что pthread_cond_wait не обязательно может вернуть управление по вызову pthread_cond_signal из посылающего данные потока
		ERROR_IF_NON_ZERO( pthread_cond_wait(&info->cond, &info->lock) );
	    /* важно понимать что функция pthread_cond_wait большую часть времени держит мьютекс разблокированным.
		   при получении сигнала от другого потока, pthread_cond_wait опять блокирует мьютекс и возвращает управление
		   текущему потоку.
		 */
    /* копируем данные в текущий поток и разблокируем мьютекс.
	   таким образом мы блокируем мьютекс на короткий промежуток времени, только чтобы скопировать данные.
	   все остальное время потоки действуют параллельно */
	strcpy( buf, info->mailbox );
	info->mailbox_status = EMPTY;
	ERROR_IF_NON_ZERO( pthread_mutex_unlock(&info->lock) );
}

/* аналог printf, только можно выводить из многих потоков.
   если использовать стандартный printf для вывода из многих потоков
   то программа будет вылетать с ошибкой периодически, т.к. стандартный
   printf не предназначен для многопоточности */
int mt_printf(const char *format, ...)
{
	int ret;
	va_list ap;
	va_start(ap, format);
	ERROR_IF_NON_ZERO( pthread_mutex_lock(&global_lock) );
	ret = vprintf(format, ap);
	ERROR_IF_NON_ZERO( pthread_mutex_unlock(&global_lock) );
	va_end(ap);
	return ret;
}

void* thread1(void* info_void)
{ 
  thread_info* info = (thread_info*)info_void;
	char in_buf[IN_BUF_SIZE];
	thread_info *next_thread = &thread_infos[1];
	while(1)
	{
		receive_data(in_buf, info);
		info->mode_fun(in_buf);
		mt_printf("[thread 1] \"%s\"\n", in_buf);
		send_data(in_buf, next_thread);
    }
}

void* thread2(void* info_void)
{
	thread_info* info = (thread_info*)info_void;
	char in_buf[IN_BUF_SIZE];
	while(1)
	{
		receive_data(in_buf, info);
		info->mode_fun(in_buf);
		mt_printf("[thread 2] \"%s\"\n", in_buf);
    }
}

void sigint_handler(int arg)
{
	if(st != NORMAL)
	{
		exit(arg);
	}
	mt_printf("\nChoose thread number 1-%i:\n", NUM_THREADS);
	st = CHOOSE_THREAD;
}

void translate(char *str)
{
	
}
void reverse(char *str)
{
	
	int i;
	int len = strlen(str);
	int size = len/2;
	char c;
	for(i=0; i<size; i++)
	{
		c = str[i];
		str[i] = str[len-i-1];
		str[len-i-1] = c;		
	}
}
void swap(char *str)
{
	int i;
	int len = strlen(str);
	int size = len/2;
	char c;
	for(i=0; i<size; i++)
	{
		c = str[i*2];
		str[i*2] = str[i*2+1];
		str[i*2+1] = c;		
	}
}
void koi8(char *str)
{
	int i;
	int len = strlen(str);
	for(i=0; i<len; i++)
	{
		str[i] |= 0x80;
	}
}

void to_upper(char *str)
{
	int i;
	int len = strlen(str);
	for(i=0; i<len; i++)
	{
		str[i] = toupper(str[i]);
	}
}

void to_lower(char *str)
{
	int i;
	int len = strlen(str);
	for(i=0; i<len; i++)
	{
		str[i] = tolower(str[i]);
	}
}

void invert_case(char *str)
{
	int i;
	int len = strlen(str);
	for(i=0; i<len; i++)
	{
		if(isupper(str[i]))
		{
			str[i] = tolower(str[i]);
		}else if(islower(str[i]))
		{
			str[i] = toupper(str[i]);
		}
	}
}
