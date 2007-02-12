
#include <string.h>

#include <unistd.h>

#include <glib.h>

#include "udp-fake.h"

typedef struct _Packet Packet;

struct _Packet
{
  NiceAddress addr;
  guint len;
  gchar buf[1024];
};

typedef struct _UDPFakeSocketPriv UDPFakeSocketPriv;

struct _UDPFakeSocketPriv
{
  GSList *send_queue;
  guint recv_pipe_in;
  guint recv_pipe_out;
};

static void *
_g_slist_pop (GSList **list)
{
  void *data;
  GSList *head;

  if (*list == NULL)
    return NULL;

  head = *list;
  data = (*list)->data;
  *list = (*list)->next;
  g_slist_free_1 (head);
  return data;
}

static gboolean
fake_send (
  NiceUDPSocket *sock,
  NiceAddress *to,
  guint len,
  const gchar *buf)
{
  Packet *packet;
  UDPFakeSocketPriv *priv;

  packet = g_slice_new0 (Packet);
  packet->len = len;
  packet->addr = *to;
  memcpy (packet->buf, buf, len);

  priv = (UDPFakeSocketPriv *) sock->priv;
  priv->send_queue = g_slist_append (priv->send_queue, packet);

  return TRUE;
}

static gint
fake_recv (
  NiceUDPSocket *sock,
  NiceAddress *from,
  guint len,
  gchar *buf)
{
  UDPFakeSocketPriv *priv;

  priv = (UDPFakeSocketPriv *) sock->priv;

  read (priv->recv_pipe_out, from, sizeof (NiceAddress));
  read (priv->recv_pipe_out, &len, sizeof (guint));
  read (priv->recv_pipe_out, buf, len);

  return len;
}

static void
fake_close (NiceUDPSocket *sock)
{
  UDPFakeSocketPriv *priv;

  priv = (UDPFakeSocketPriv *) sock->priv;
  close (priv->recv_pipe_out);
  close (priv->recv_pipe_in);

  while (priv->send_queue)
    {
      Packet *packet;

      packet = priv->send_queue->data;
      priv->send_queue = g_slist_remove (priv->send_queue, packet);
      g_slice_free (Packet, packet);
    }

  g_slice_free (UDPFakeSocketPriv, priv);
}

/* XXX: copied INADDR_ANY to sock->addr rather than using a valid address */
static gboolean
fake_socket_init (
  G_GNUC_UNUSED
  NiceUDPSocketFactory *man,
  NiceUDPSocket *sock,
  NiceAddress *addr)
{
  int fds[2];
  static int port = 1;
  UDPFakeSocketPriv *priv;

  if (pipe (fds) == -1)
    return FALSE;

  priv = g_slice_new0 (UDPFakeSocketPriv);
  priv->recv_pipe_out = fds[0];
  priv->recv_pipe_in = fds[1];

  sock->fileno = priv->recv_pipe_out;
  sock->addr.type = addr->type;
  sock->addr.addr_ipv4 = addr->addr_ipv4;

  if (addr->port == 0)
    sock->addr.port = port++;
  else
    sock->addr.port = addr->port;

  sock->send = fake_send;
  sock->recv = fake_recv;
  sock->priv = priv;
  sock->close = fake_close;
  return TRUE;
}

void
nice_udp_fake_socket_push_recv (
  NiceUDPSocket *sock,
  NiceAddress *from,
  guint len,
  gchar *buf)
{
  UDPFakeSocketPriv *priv;

  priv = (UDPFakeSocketPriv *) sock->priv;
  write (priv->recv_pipe_in, from, sizeof (NiceAddress));
  write (priv->recv_pipe_in, &len, sizeof (guint));
  write (priv->recv_pipe_in, buf, len);
}

guint
nice_udp_fake_socket_pop_send (
  NiceUDPSocket *sock,
  NiceAddress *to,
  guint len,
  gchar *buf)
{
  UDPFakeSocketPriv *priv;
  Packet *packet;

  priv = (UDPFakeSocketPriv *) sock->priv;
  packet = (Packet *) _g_slist_pop (&priv->send_queue);

  if (!packet)
    return 0;

  memcpy (buf, packet->buf, MIN (len, packet->len));
  len = packet->len;
  *to = packet->addr;
  g_slice_free (Packet, packet);
  return len;
}

static void
fake_socket_factory_close (
  G_GNUC_UNUSED
  NiceUDPSocketFactory *man)
{
}

void
nice_udp_fake_socket_factory_init (NiceUDPSocketFactory *man)
{
  man->init = fake_socket_init;
  man->close = fake_socket_factory_close;
  man->priv = NULL;
}

