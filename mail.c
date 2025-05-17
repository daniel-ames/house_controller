// copyright, your mom. Last night.


#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <fcntl.h>

#include <auth-client.h>
#include <libesmtp.h>

#include "private_auth.h"
#include "controller.h"


/*
These 2 defines are to be defined in a non-versioned include file, private_auth.h (for obvious reasons).
For here, they are simply example defines to help you remember how this all works.
#define EMAIL_USERNAME  "gmail_user_name"  // without the @gmail.com
#define EMAIL_APP_PASSWORD  "aaaa bbbb cccc dddd" // 16 character app password (actually 19 with the spaces) that you made for the account (https://myaccount.google.com/apppasswords)
*/

static int authinteract (auth_client_request_t request, char **result, int fields, void *arg);
static void event_cb (smtp_session_t session, int event_no, void *arg,...);
static void print_recipient_status (smtp_recipient_t recipient, const char *mailbox, void *arg);

int send_email() {
  smtp_session_t session = smtp_create_session();
  smtp_message_t message = smtp_add_message(session);
  smtp_recipient_t recipient;
  const smtp_status_t *status;
  enum notify_flags notify = Notify_NOTSET;

  #ifdef USE_TLS
  printf("\n\nUSE_TLS\n\n");
  #else
  printf("\n\nNO USE_TLS\n\n");
  #endif
  int res = smtp_starttls_enable(session, Starttls_REQUIRED);
  printf("smtp_starttls_enable res: %d\n", res);

  auth_context_t authctx = auth_create_context ();
  auth_set_mechanism_flags (authctx, AUTH_PLUGIN_PLAIN, 0);
  auth_set_interact_cb (authctx, authinteract, NULL);
  
  smtp_set_eventcb(session, event_cb, NULL);
  smtp_auth_set_context (session, authctx);

  smtp_set_header(message, "From", "House Controller", "ameshousecontroller@gmail.com");
  smtp_set_header(message, "To", NULL, "danieladamames@gmail.com");
  smtp_set_header(message, "Subject", NULL, "foo email");
  smtp_set_header_option(message, "Subject", Hdr_OVERRIDE, 1);
  smtp_set_server(session, "smtp.gmail.com:587");

  FILE* fd = fopen(MEASUREMENT_FILE, "r");
  
  //smtp_set_message_fp(message, fd);
  smtp_set_messagecb (message, _smtp_message_fp_cb, fd);

  recipient = smtp_add_recipient(message, "danieladamames@gmail.com");
  notify = Notify_SUCCESS | Notify_FAILURE | Notify_DELAY;
  smtp_dsn_set_notify (recipient, notify);

  if (!smtp_start_session(session)) {
    printf("failed to start session\n");
    smtp_destroy_session(session);
    auth_destroy_context (authctx);
    auth_client_exit();
    fclose(fd);
    return 1;
  }

  status = smtp_message_transfer_status(message);
  printf("status code: %d, status text: \"%s\"\n", status->code, status->text == NULL ? "NULL" : status->text);
  smtp_enumerate_recipients (message, print_recipient_status, NULL);

  smtp_destroy_session(session);
  auth_destroy_context (authctx);
  auth_client_exit();
  fclose(fd);
  return 0;
}


static
void
print_recipient_status (smtp_recipient_t recipient, const char *mailbox, void *arg)
{
  const smtp_status_t *status;

  status = smtp_recipient_status (recipient);
  printf ("%s: %d %s\n", mailbox, status->code, status->text);
}

static
int
authinteract (auth_client_request_t request, char **result, int fields, void *arg)
{
  char prompt[64];
  static char resp[512];
  char *p, *rp;
  int i, n, tty;

  char mailbox_user[] = EMAIL_USERNAME;
  char app_password[] = EMAIL_APP_PASSWORD;

  rp = resp;
  for (i = 0; i < fields; i++)
  {
    n = snprintf (prompt, sizeof prompt, "%s%s: ", request[i].prompt, (request[i].flags & AUTH_CLEARTEXT) ? " (not encrypted)" : "");
    if (request[i].flags & AUTH_PASS)
	    //result[i] = getpass (prompt);
      result[i] = app_password;
    else
	  {
      result[i] = mailbox_user;
	  }
  }
  return 1;
}



static
void
event_cb (smtp_session_t session, int event_no, void *arg,...)
{
  va_list alist;
  int *ok;
  smtp_status_t *status;
  smtp_message_t msg_ptr;
  smtp_recipient_t recipient;
  char *mailbox;
  int len = 0;

  va_start(alist, arg);
  switch(event_no) {
  case SMTP_EV_CONNECT: printf("connected\n"); break;
  case SMTP_EV_MAILSTATUS:
    mailbox = va_arg(alist, char*);
    msg_ptr = va_arg(alist, smtp_message_t);
    status = smtp_reverse_path_status(msg_ptr);
    printf("MAILSTATUS - mailbox: %s\n", mailbox);
    printf("MAILSTATUS - status code: %d text: %s\n", status->code, status->text == NULL ? "null" : status->text);
    break;
  case SMTP_EV_RCPTSTATUS:
    mailbox = va_arg(alist, char*);
    recipient = va_arg(alist, smtp_recipient_t);
    status = smtp_recipient_status(recipient);
    printf("SMTP_EV_RCPTSTATUS - mailbox: %s\n", mailbox);
    printf("SMTP_EV_RCPTSTATUS - status code: %d text: %s\n", status->code, status->text == NULL ? "null" : status->text);
    break;
  case SMTP_EV_MESSAGEDATA:
    msg_ptr = va_arg(alist, smtp_message_t);
    status = smtp_reverse_path_status(msg_ptr);
    len = va_arg(alist, int);
    printf("SMTP_EV_MESSAGEDATA - length: %d\n", len);
    printf("SMTP_EV_MESSAGEDATA - status code: %d text: %s\n", status->code, status->text == NULL ? "null" : status->text);
    break;
  case SMTP_EV_MESSAGESENT:
    msg_ptr = va_arg(alist, smtp_message_t);
    status = smtp_reverse_path_status(msg_ptr);
    printf("SMTP_EV_MESSAGESENT - status code: %d text: %s\n", status->code, status->text == NULL ? "null" : status->text);
    break;
  case SMTP_EV_DISCONNECT:
    printf("SMTP_EV_DISCONNECT\n");
    break;
  case SMTP_EV_WEAK_CIPHER: {
    int bits;
    bits = va_arg(alist, long); ok = va_arg(alist, int*);
    printf("SMTP_EV_WEAK_CIPHER, bits=%d - accepted.\n", bits);
    *ok = 1; break;
  }
  case SMTP_EV_STARTTLS_OK:
    puts("SMTP_EV_STARTTLS_OK - TLS started here."); break;
  case SMTP_EV_INVALID_PEER_CERTIFICATE: {
    long vfy_result;
    vfy_result = va_arg(alist, long); ok = va_arg(alist, int*);
    //*ok = handle_invalid_peer_certificate(vfy_result);
    break;
  }
  case SMTP_EV_NO_PEER_CERTIFICATE: {
    ok = va_arg(alist, int*); 
    puts("SMTP_EV_NO_PEER_CERTIFICATE - accepted.");
    *ok = 1; break;
  }
  case SMTP_EV_WRONG_PEER_CERTIFICATE: {
    ok = va_arg(alist, int*);
    puts("SMTP_EV_WRONG_PEER_CERTIFICATE - accepted.");
    *ok = 1; break;
  }
  case SMTP_EV_NO_CLIENT_CERTIFICATE: {
    ok = va_arg(alist, int*); 
    puts("SMTP_EV_NO_CLIENT_CERTIFICATE - accepted.");
    *ok = 1; break;
  }
  case SMTP_EV_EXTNA_DSN: {
    ok = va_arg(alist, int*); 
    printf("SMTP_EV_EXTNA_DSN - ok: %d\n", *ok);
    //*ok = 1;
    break;
  }
  default:
    printf("Got event: %d - ignored.\n", event_no);
  }
  va_end(alist);
}