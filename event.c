

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
//#include <linux/time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <poll.h>

#include "event_poll.h"


#include "list.h"
typedef struct {

	struct listnode fd_list;
	struct listnode timer_list;
	pthread_mutex_t lock;
	int recv_fd;
	int send_fd;
}event_list_t;


typedef struct{
	int interval;
	int id;
	//struct timespec ts;
	long int  t;
	void * arg;
	int flags;
	int (*callback) ( void *arg);
	struct listnode list;
}timer_list_t;


typedef struct{
	int fd;
	int id;
	short int events;
	void * arg;
	int deleted;
	int (*callback) (int fd, short int events, void *arg);
	struct listnode list;
}fd_t;


static void list_init(struct listnode *node)
{
	node->next = node;
	node->prev = node;
}

static void list_add_tail(struct listnode *head, struct listnode *item)
{
	item->next = head;
	item->prev = head->prev;
	head->prev->next = item;
	head->prev = item;
}

static void list_remove(struct listnode *item)
{
	item->next->prev = item->prev;
	item->prev->next = item->next;
}



event_poll_t * event_init(void){
	event_poll_t * e;
	event_list_t * l;
	int s[2];
	e = malloc (sizeof(event_poll_t));
	l = malloc (sizeof(event_list_t));

	e->ctx = l;
	list_init(&l->fd_list);
	list_init(&l->timer_list);

	if(pthread_mutex_init(&l->lock, NULL)){
		free(l);
		free(e);
		return NULL;
	}


	if (socketpair( AF_UNIX , SOCK_STREAM, 0, s) == 0) {
		l->send_fd = s[0];
		l->recv_fd = s[1];
		fcntl(s[0], F_SETFD, FD_CLOEXEC);
		fcntl(s[0], F_SETFL, O_NONBLOCK);
		fcntl(s[1], F_SETFD, FD_CLOEXEC);
		fcntl(s[1], F_SETFL, O_NONBLOCK);
	}else{
		pthread_mutex_destroy(&l->lock);
		free(l);
		free(e);
		return NULL;
	}


	return e;

}


static int _event_remove_fd(fd_t *fd){

	list_remove(&fd->list);
	free(fd);
	return 0;
}

static int _event_remove_timer(timer_list_t *t){

	list_remove(&t->list);
	free(t);
	return 0;
}

static int find_fd_id(event_list_t * l){
	int i;
	struct listnode *node;
	for(i=1;i<0xFFFFFFF;i++){
		int found = 0;
		list_for_each(node, &l->fd_list) {
			fd_t *fd = node_to_item(node, fd_t, list);
			if(fd->id == i){
				found = 1;
				break;
			}
		}
		if(found == 0)
			return i;
	}
	return -1;
}


int event_remove_fd(event_poll_t *e,int fd){

	event_list_t * l;
	struct listnode *node;
	int s;

	if(e == NULL){
		return -1;
	}
	l = e->ctx;
	if(fd < 0)
		return -1;
	pthread_mutex_lock(&l->lock);
	list_for_each(node, &l->fd_list) {
		fd_t *_fd = node_to_item(node, fd_t, list);
		if(_fd->id == fd){
			_fd->deleted = 1;
			write(l->send_fd, &s, 1);
			pthread_mutex_unlock(&l->lock);
			return 0;

		}
	}
	pthread_mutex_unlock(&l->lock);

	return -1;

}


int event_add_fd(event_poll_t *e,int fd,short int events,void * arg, event_poll_callback_t  callback){

	event_list_t * l;
	fd_t * fd_l;
	int s;
	int id;
	if(e == NULL){
		return -1;
	}

	l = e->ctx;

	if(fd < 0)
		return -1;

	fd_l = malloc(sizeof(fd_t));

	if(fd_l == NULL)
		return -1;
	memset(fd_l,'\0',sizeof(fd_t));
	fd_l->fd = fd;
	fd_l->callback = callback;
	fd_l->events = events;
	fd_l->arg = arg;

	pthread_mutex_lock(&l->lock);
	id = find_fd_id(l);
	if(id < 0){
		free(fd_l);
		pthread_mutex_unlock(&l->lock);
		return -1;
	}
	fd_l->id = id;
	list_add_tail(&l->fd_list,&fd_l->list);
	write(l->send_fd, &s, 1);
	pthread_mutex_unlock(&l->lock);
	return fd_l->id;
}

static int find_timer_id(event_list_t * l){
	int i;
	struct listnode *node;
	for(i=1;i<0xFFFFFFF;i++){
		int found = 0;
		list_for_each(node, &l->timer_list) {
			timer_list_t *t = node_to_item(node, timer_list_t, list);
			if(t->id == i){
				found = 1;
				break;
			}
		}
		if(found == 0)
			return i;
	}
	return -1;
}


int event_remove_timer(event_poll_t *e,int fd){

	event_list_t * l;
	struct listnode *node;
	int s;

	if(e == NULL){
		return -1;
	}
	l = e->ctx;
	if(fd < 0)
		return -1;
	pthread_mutex_lock(&l->lock);
	list_for_each(node, &l->timer_list) {
		timer_list_t *t = node_to_item(node, timer_list_t, list);
		if(t->id == fd){
			t->flags |= 1;
			write(l->send_fd, &s, 1);
			pthread_mutex_unlock(&l->lock);
			return 0;

		}
	}
	pthread_mutex_unlock(&l->lock);

	return -1;

}

int event_add_timer(event_poll_t *e,int interval,void * arg, event_timer_callback_t  callback){

	event_list_t * l;
	timer_list_t * t;
	int s;
	int id;
	if(e == NULL){
		return -1;
	}

	l = e->ctx;

	if(interval < 0)
		return -1;

	t = malloc(sizeof(timer_list_t));

	if(t == NULL)
		return -1;

	memset(t,'\0',sizeof(timer_list_t));

	t->flags = 4;
	t->interval = interval;

	t->callback = callback;

	t->arg = arg;
	pthread_mutex_lock(&l->lock);
	id = find_timer_id(l);
	if(id < 0 ){
		free(t);
		pthread_mutex_unlock(&l->lock);
		return -1;
	}
	t->id = id;
	list_add_tail(&l->timer_list,&t->list);
	write(l->send_fd, &s, 1);
	pthread_mutex_unlock(&l->lock);
	return t->id;
}


long int _gettime()
{

	int ret;
	struct timespec ts;
	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {

		return 0;
	}

	return ts.tv_sec * 1000 + (long int)(ts.tv_nsec / 1000000);
}



int event_main_loop(event_poll_t *e){


	struct pollfd ufds[256];
	event_list_t * l;
	long int time_dif;
	int fd_count = 0;

	if(e == NULL){
		return -1;
	}

	l = e->ctx;



	for(;;) {
		int nr, i, timeout = -1;
		fd_count = 0;

		struct listnode *node;

		ufds[fd_count].fd = l->recv_fd;
		ufds[fd_count].events = POLLIN;
		ufds[fd_count].revents = 0;
		fd_count++;

		list_for_each(node, &l->fd_list) {
			fd_t *fd = node_to_item(node, fd_t, list);
			if(fd->fd < 0){
				fd->deleted = 1;
				continue;
			}
			fd->deleted = 0;
			ufds[fd_count].fd = fd->fd;
			ufds[fd_count].events = fd->events;
			ufds[fd_count].revents = 0;
			fd_count++;
		}


		list_for_each(node, &l->timer_list) {
			timer_list_t *t = node_to_item(node, timer_list_t, list);
			if(t->interval < 0){
				t->flags |= 1;
				continue;
			}
			t->flags &= ~0x01;
			if((t->flags & 4 ) == 4){
				//_settime(&t->ts,t->interval);
				t->t = _gettime() + t->interval;
				t->flags &= ~0x04;
			}
			time_dif = t->t - _gettime();
			if(time_dif < 0 ){
				timeout = 0;
				time_dif = 0;
			}

			if(timeout == -1)
				timeout = time_dif;

			if(  timeout > time_dif){
				timeout = time_dif;
			}

		}


		if(timeout < 0 || timeout > 10000){
			timeout = 10000;
		}

		//if(fd_count > 0){
			nr = poll(ufds, fd_count, timeout);
		/*}else{
			if(timeout > 0 )
				usleep(timeout*1000);
			nr = 0;
		}*/
		if (nr < 0)
			continue;

		for (i = 0; i < fd_count; i++) {


			if( ufds[i].fd == l->recv_fd &&(ufds[i].revents &  POLLIN ) ){
				char tmp[32];
				read(l->recv_fd, tmp, sizeof(tmp));
			}
			else list_for_each(node, &l->fd_list) {
				fd_t *fd = node_to_item(node, fd_t, list);

				if(fd->deleted){
					continue;
				}
				if(fd->fd == ufds[i].fd  ){

					do {
						if(ufds[i].revents & fd->events & POLLIN ){
							if( fd->callback(fd->fd,POLLIN,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}

						if(ufds[i].revents & fd->events & POLLPRI ){
							if( fd->callback(fd->fd,POLLPRI,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}

						if(ufds[i].revents & fd->events & POLLOUT ){
							if( fd->callback(fd->fd,POLLOUT,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}

						if(ufds[i].revents & fd->events & POLLERR ){
							if( fd->callback(fd->fd,POLLERR,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}
						if(ufds[i].revents & fd->events & POLLHUP ){
							if( fd->callback(fd->fd,POLLHUP,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}
						if(ufds[i].revents & fd->events & POLLNVAL ){
							if( fd->callback(fd->fd,POLLNVAL,fd->arg)){
								fd->deleted = 1;
								break;
							}
						}
					}while(0);


				}

			}

		}


		list_for_each(node, &l->timer_list) {
			timer_list_t *t = node_to_item(node, timer_list_t, list);
			if(t->flags & 1 ){
				continue;
			}
			time_dif = t->t - _gettime();

			if(time_dif <= 0 ){
				t->t = _gettime() + t->interval;

				if( t->callback(t->arg)){
					t->flags |= 1;
				}
			}
		}



		{

			timer_list_t *t ;

			node = (&l->timer_list)->next;
			for ( ;; ){
				if(node == (&l->timer_list)){
					break;
				}
				t = node_to_item(node, timer_list_t, list);
				node = node->next;
				if(t->flags & 1 ){
					_event_remove_timer(t);
				}
			}
		}

		{

			fd_t *fd_l ;
			node = (&l->fd_list)->next;
			for ( ;; ){

				if(node == (&l->fd_list)){
					break;
				}
				fd_l = node_to_item(node, fd_t, list);
				node = node->next;
				if(fd_l->deleted){
					_event_remove_fd(fd_l);
				}
			}
		}

	}


	return 0;

}