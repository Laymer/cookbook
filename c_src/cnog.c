#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>
#include "cnog.h"

static ei_cnode ec;                    /* the ei cnode description struct */

erlang_pid *cnog_self(void) {
  return ei_self(&ec);
}

static void ABEND(char* reason) {
  printf("ABEND: %s\n", reason);
  exit(1);
}

void cnog_send(ei_x_buff *xbuf, cnog_dest *dest) {

  if ( ei_send(dest->fd, &dest->pid, xbuf->buff, xbuf->index) )
    ABEND("bad send");
}

static void make_xbuf(ei_x_buff *xbuf) {
  ei_x_new_with_version(xbuf);
  cnog_wrap_ans("reply",xbuf);
}

static void send_xbuf(ei_x_buff *xbuf, cnog_dest *dest){
  cnog_send(xbuf, dest);
  ei_x_free(xbuf);
}

static bool get_fpointer(char *func, ei_x_buff *xbuf, void **funcp) {
  static void* library;
  char *error;

  if ( ! library ) library = dlopen(NULL,0);
  dlerror();                                     /* Clear any existing error */
  *funcp = dlsym(library, func);          /* this is where the magic happens */
  if ( (error = dlerror()) == NULL ) return true;          /* \o/ it worked! */

  fprintf(stderr, "could not find '%s', error %s\n", func, error);
  cnog_enc_2_error(xbuf, "no_such_function");
  ei_x_encode_atom(xbuf, func);
  return false;
}

static bool make_reply(ei_x_buff *xbuf, char *buff, int *index, cnog_dest *dest) {
  int k;
  int arity;
  char cmd[MAXATOMLEN+1];
  char wrapped_cmd[MAXATOMLEN+MAXATOMLEN+2] = "";
  bool (*funcp)(int, ei_x_buff *, char *, int *);

  if ( ! ((arity = cnog_get_tuple(xbuf, buff, index)) > -1) ||
       ! cnog_check_arity(xbuf,2,arity)  ||
       ! cnog_get_arg_atom(xbuf, buff, index, cmd) ||
       ! ((arity = cnog_get_list(xbuf, buff, index)) > -1) )
    return false;

  strcat(wrapped_cmd,dest->prefix);
  strcat(wrapped_cmd,"_");
  strcat(wrapped_cmd,cmd);
  if ( get_fpointer(wrapped_cmd, xbuf, (void *)&funcp) &&
       (*funcp)(arity, xbuf, buff, index) ) {
    assert( ei_decode_list_header(buff,index,&k) == 0 );
    assert( k == 0 );
    return true;
  }

  return false;
}

static void send_replies(char *buff, int *index, cnog_dest *dest) {
  ei_x_buff xbuf;
  int arity, i;

  make_xbuf(&xbuf);

  if ( (arity = cnog_get_list(&xbuf, buff, index)) < 0 ) {
    send_xbuf(&xbuf, dest);
    return;
  }

  for (i = 0; i < arity; i++) {
    if ( ((i+1)%1000) == 0 ) {
      ei_x_encode_empty_list(&xbuf);
      send_xbuf(&xbuf, dest);
      make_xbuf(&xbuf);
    }
    ei_x_encode_list_header(&xbuf, 1);
    if ( ! make_reply(&xbuf, buff, index, dest) )
      break;
  }

  ei_x_encode_empty_list(&xbuf);
  send_xbuf(&xbuf, dest);
}

static void reply(ei_x_buff *recv_x_buf, cnog_dest *dest) {
  int index = 0;
  int version;

  ei_decode_version(recv_x_buf->buff, &index, &version);
  send_replies(recv_x_buf->buff, &index, dest);
}

static bool cnog_receive(cnog_dest *dest) {
  erlang_msg msg;
  ei_x_buff xbuf;

  ei_x_new_with_version(&xbuf);
  switch ( ei_xreceive_msg(dest->fd, &msg, &xbuf) ){
  case ERL_MSG:
    switch (msg.msgtype) {
    case ERL_REG_SEND:
      dest->pid = msg.from;
      strcpy(dest->prefix, msg.toname);
      reply(&xbuf, dest);
      break;
    case ERL_SEND:
    case ERL_LINK:
    case ERL_UNLINK:
    case ERL_GROUP_LEADER:
      break;                    /* ignore */
    case ERL_EXIT:
    case ERL_EXIT2:
    case ERL_NODE_LINK:
      return false;             /* die */
    }
    break;
  case ERL_TICK:
    break;                      /* ignore */
  case ERL_ERROR:
    return false;               /* die */
  }
  ei_x_free(&xbuf);
  return true;
}

static int cnog_loop(cnog_dest *dest) {
  while (1) {
    if ( ! cnog_receive(dest) ) return 1;
  }
}

#define REM_NODE_NAME     argv[1]
#define COOKIE            argv[2]
#define NODE_NAME         argv[3]
#define ERL_DIST_VSN atoi(argv[4])

static void cnog_start_cnode(char **argv, cnog_dest *dest) {
  erlang_pid *self = cnog_self();
  int fd;

  printf("I am %s, you are %s (%d)\n", NODE_NAME, REM_NODE_NAME, ERL_DIST_VSN);

  ei_set_compat_rel(ERL_DIST_VSN); /* erlnode version of dist. protocol */

  if ( ei_connect_init(&ec, NODE_NAME, COOKIE, 1) < 0 )
    ABEND("ei_connect_init");

  if ( (fd = ei_connect(&ec, REM_NODE_NAME)) < 0 )
    ABEND("ei_connect failed.\nwrong cookie? erl-dist version mismatch?");

  self->num = fd;               /* bug?? in ei_reg_send_tmo */
  dest->fd = fd;
}

int main(int argc, char **argv){
  cnog_dest dest;

  if ( argc != 5 ){
    printf("Usage: %s rem_node_name cookie node_name erl_dist_vsn\n", argv[0]);
    return 1;
  }

  cnog_start_cnode(argv, &dest);
  cnog_loop(&dest);
  return 0;
}
