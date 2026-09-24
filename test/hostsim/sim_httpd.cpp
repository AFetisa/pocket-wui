// A small esp_http_server for the host simulator. Behaviour mirrored from
// ESP-IDF where the firmware relies on it:
//  * one thread runs every handler, one request at a time;
//  * httpd_req_async_handler_begin() hands the socket to another thread and the
//    server leaves that connection alone until ..._complete();
//  * req->uri keeps the query string; matching stops at '?';
//  * request headers beyond max_req_hdr_len -> 431, URIs beyond max_uri_len -> 414;
//  * more than max_resp_headers httpd_resp_set_hdr() calls fail;
//  * unread body bytes are discarded after the handler returns;
//  * a handler returning ESP_FAIL closes the connection.
#include <esp_http_server.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Route { std::string uri; httpd_method_t method; esp_err_t (*fn)(httpd_req_t *); };

struct Server {
  httpd_config_t cfg;
  int lfd = -1;
  int wake[2] = {-1, -1};                // self-pipe: an async request finished
  std::vector<Route> routes;
  httpd_err_handler_func_t err404 = nullptr;
  std::mutex mu;
  struct Conn { std::string pending; bool busy = false; };
  std::map<int, Conn> conns;
  std::thread thr;
  std::atomic<bool> stop{false};
};

struct Aux {
  Server *s;
  int fd;
  std::string method, path, version;
  std::vector<std::pair<std::string, std::string>> hdrs;
  size_t bodyLeft = 0;
  // response
  std::string status = "200 OK", type = "text/html";
  std::vector<std::pair<const char *, const char *>> out;   // pointers, like IDF
  bool chunked = false, sentHead = false, async = false, closeAfter = false;
};

Aux *aux(httpd_req_t *r) { return (Aux *)r->aux; }

const char *kMethods[] = {"DELETE", "GET", "HEAD", "POST", "PUT", "CONNECT", "OPTIONS", "TRACE", "COPY",
                          "LOCK", "MKCOL", "MOVE", "PROPFIND", "PROPPATCH", "SEARCH", "UNLOCK"};

int methodId(const std::string &m) {
  for (int i = 0; i < (int)(sizeof(kMethods) / sizeof(*kMethods)); ++i)
    if (m == kMethods[i]) return i;
  return -1;
}

bool ieq(const std::string &a, const char *b) {
  size_t n = strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}

bool sendAll(int fd, const char *p, size_t n) {
  while (n) {
    ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
    if (k <= 0) return false;
    p += k; n -= k;
  }
  return true;
}

// Reads from the connection's pending buffer first, then the socket.
int readSome(Server *s, int fd, char *buf, size_t n, int timeoutS) {
  {
    std::lock_guard<std::mutex> l(s->mu);
    auto &p = s->conns[fd].pending;
    if (!p.empty()) {
      size_t k = std::min(n, p.size());
      memcpy(buf, p.data(), k);
      p.erase(0, k);
      return (int)k;
    }
  }
  pollfd pf{fd, POLLIN, 0};
  int pr = poll(&pf, 1, timeoutS * 1000);
  if (pr == 0) return HTTPD_SOCK_ERR_TIMEOUT;
  if (pr < 0) return HTTPD_SOCK_ERR_FAIL;
  ssize_t k = recv(fd, buf, n, 0);
  return k <= 0 ? HTTPD_SOCK_ERR_FAIL : (int)k;
}

void sendHead(httpd_req_t *r, long contentLength) {
  Aux *a = aux(r);
  std::string h = "HTTP/1.1 " + a->status + "\r\nContent-Type: " + a->type + "\r\n";
  if (contentLength >= 0) h += "Content-Length: " + std::to_string(contentLength) + "\r\n";
  else h += "Transfer-Encoding: chunked\r\n";
  for (auto &kv : a->out) h += std::string(kv.first) + ": " + kv.second + "\r\n";
  h += "\r\n";
  sendAll(a->fd, h.data(), h.size());
  a->sentHead = true;
}

void simpleReply(int fd, const char *status, const char *body) {
  std::string h = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: text/plain\r\nContent-Length: " +
                  std::to_string(strlen(body)) + "\r\n\r\n" + body;
  sendAll(fd, h.data(), h.size());
}

// Reads one request head. 0 = ok, -1 = connection gone, else an HTTP error code.
int readHead(Server *s, int fd, std::string &head) {
  char buf[2048];
  for (;;) {
    size_t e = head.find("\r\n\r\n");
    if (e != std::string::npos) {
      std::lock_guard<std::mutex> l(s->mu);
      s->conns[fd].pending = head.substr(e + 4) + s->conns[fd].pending;
      head.resize(e + 2);
      return 0;
    }
    if (head.size() > s->cfg.max_req_hdr_len + s->cfg.max_uri_len + 64) return 431;
    int k = readSome(s, fd, buf, sizeof(buf), s->cfg.recv_wait_timeout);
    if (k <= 0) return -1;
    head.append(buf, k);
  }
}

bool matchRoute(Server *s, const Route &rt, const std::string &path) {
  if (s->cfg.uri_match_fn) return s->cfg.uri_match_fn(rt.uri.c_str(), path.c_str(), path.size());
  return rt.uri == path;
}

// Serves one request on fd. Returns false when the connection must close.
bool serveOne(Server *s, int fd) {
  std::string head;
  int rc = readHead(s, fd, head);
  if (rc == -1) return false;
  if (rc == 431) { simpleReply(fd, "431 Request Header Fields Too Large", "header fields are too long\n"); return false; }

  auto *a = new Aux();
  a->s = s;
  a->fd = fd;
  size_t lineEnd = head.find("\r\n");
  std::string line = head.substr(0, lineEnd);
  size_t sp1 = line.find(' '), sp2 = line.rfind(' ');
  if (sp1 == std::string::npos || sp2 == sp1) { simpleReply(fd, "400 Bad Request", "bad request line\n"); delete a; return false; }
  a->method = line.substr(0, sp1);
  std::string uri = line.substr(sp1 + 1, sp2 - sp1 - 1);
  a->version = line.substr(sp2 + 1);
  size_t hdrBytes = 0;
  for (size_t p = lineEnd + 2; p < head.size();) {
    size_t e = head.find("\r\n", p);
    std::string h = head.substr(p, e - p);
    p = e + 2;
    size_t c = h.find(':');
    if (c == std::string::npos) continue;
    std::string v = h.substr(c + 1);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    a->hdrs.push_back({h.substr(0, c), v});
    hdrBytes += h.size() + 2;
  }

  httpd_req_t *r = new httpd_req_t();
  r->handle = s;
  r->aux = a;
  for (auto &kv : a->hdrs) {
    if (ieq(kv.first, "Content-Length")) r->content_len = strtoull(kv.second.c_str(), nullptr, 10);
    if (ieq(kv.first, "Connection") && ieq(kv.second, "close")) a->closeAfter = true;
  }
  a->bodyLeft = r->content_len;
  auto finish = [&](bool keep) {
    bool ok = keep && !a->closeAfter;
    // Discard whatever of the body the handler left unread.
    char sink[4096];
    while (ok && a->bodyLeft) {
      int k = readSome(s, fd, sink, std::min(sizeof(sink), a->bodyLeft), s->cfg.recv_wait_timeout);
      if (k <= 0) { ok = false; break; }
      a->bodyLeft -= k;
    }
    delete a;
    delete r;
    return ok;
  };

  if (uri.size() > s->cfg.max_uri_len || uri.size() >= sizeof(r->uri)) {
    simpleReply(fd, "414 URI Too Long", "URI too long\n");
    return finish(false);
  }
  if (hdrBytes > s->cfg.max_req_hdr_len) {
    simpleReply(fd, "431 Request Header Fields Too Large", "header fields are too long\n");
    return finish(false);
  }
  strcpy(r->uri, uri.c_str());
  r->method = methodId(a->method);
  std::string path = uri.substr(0, uri.find('?'));

  const Route *hit = nullptr;
  bool pathKnown = false;
  for (auto &rt : s->routes) {
    if (!matchRoute(s, rt, path)) continue;
    pathKnown = true;
    if ((int)rt.method == r->method) { hit = &rt; break; }
  }
  esp_err_t res = ESP_OK;
  if (hit) {
    res = hit->fn(r);
  } else if (pathKnown) {
    simpleReply(fd, "405 Method Not Allowed", "method not allowed\n");
  } else if (s->err404) {
    res = s->err404(r, HTTPD_404_NOT_FOUND);
  } else {
    simpleReply(fd, "404 Not Found", "not found\n");
  }
  if (a->async) {                      // the copy owns the socket now
    delete a;
    delete r;
    return true;
  }
  return finish(res == ESP_OK);
}

void closeConn(Server *s, int fd) {
  std::lock_guard<std::mutex> l(s->mu);
  s->conns.erase(fd);
  close(fd);
}

void loop(Server *s) {
  while (!s->stop) {
    std::vector<pollfd> pf{{s->lfd, POLLIN, 0}, {s->wake[0], POLLIN, 0}};
    {
      std::lock_guard<std::mutex> l(s->mu);
      for (auto &c : s->conns) if (!c.second.busy) pf.push_back({c.first, POLLIN, 0});
    }
    if (poll(pf.data(), pf.size(), 500) <= 0) continue;
    if (pf[1].revents) { char b[64]; if (read(s->wake[0], b, sizeof(b)) < 0) {} }
    if (pf[0].revents & POLLIN) {
      int fd = accept(s->lfd, nullptr, nullptr);
      if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        std::lock_guard<std::mutex> l(s->mu);
        if ((int)s->conns.size() >= s->cfg.max_open_sockets) {
          // lru_purge_enable: the device drops its oldest idle session instead.
          int victim = -1;
          for (auto &c : s->conns) if (!c.second.busy) { victim = c.first; break; }
          if (victim >= 0) { s->conns.erase(victim); close(victim); }
        }
        s->conns[fd] = Server::Conn();
      }
    }
    for (size_t i = 2; i < pf.size(); ++i) {
      if (!(pf[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      int fd = pf[i].fd;
      {
        std::lock_guard<std::mutex> l(s->mu);
        auto it = s->conns.find(fd);
        if (it == s->conns.end() || it->second.busy) continue;
      }
      if (!serveOne(s, fd)) {
        bool busy;
        { std::lock_guard<std::mutex> l(s->mu); busy = s->conns.count(fd) && s->conns[fd].busy; }
        if (!busy) closeConn(s, fd);
      }
    }
  }
}

}  // namespace

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
  auto *s = new Server();
  s->cfg = *config;
  s->lfd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config->server_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s->lfd, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(s->lfd, 16) != 0) {
    perror("httpd_start");
    close(s->lfd);
    delete s;
    return ESP_FAIL;
  }
  if (pipe(s->wake) != 0) { delete s; return ESP_FAIL; }
  s->thr = std::thread(loop, s);
  *handle = s;
  return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
  auto *s = (Server *)handle;
  s->stop = true;
  s->thr.join();
  close(s->lfd);
  return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *u) {
  auto *s = (Server *)handle;
  if ((int)s->routes.size() >= s->cfg.max_uri_handlers) return ESP_ERR_HTTPD_HANDLERS_FULL;
  for (auto &rt : s->routes)
    if (rt.uri == u->uri && rt.method == u->method) return ESP_ERR_HTTPD_HANDLER_EXISTS;
  s->routes.push_back({u->uri, u->method, u->handler});
  return ESP_OK;
}

esp_err_t httpd_register_err_handler(httpd_handle_t handle, httpd_err_code_t e, httpd_err_handler_func_t fn) {
  if (e == HTTPD_404_NOT_FOUND) ((Server *)handle)->err404 = fn;
  return ESP_OK;
}

bool httpd_uri_match_wildcard(const char *tmpl, const char *uri, size_t len) {
  size_t tl = strlen(tmpl);
  if (tl && tmpl[tl - 1] == '*') return len >= tl - 1 && strncmp(tmpl, uri, tl - 1) == 0;
  return tl == len && strncmp(tmpl, uri, len) == 0;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) { aux(r)->status = status; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) { aux(r)->type = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *f, const char *v) {
  Aux *a = aux(r);
  if (a->out.size() >= a->s->cfg.max_resp_headers) return ESP_ERR_HTTPD_RESP_HDR;
  a->out.push_back({f, v});
  return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len) {
  if (len == HTTPD_RESP_USE_STRLEN) len = buf ? (ssize_t)strlen(buf) : 0;
  sendHead(r, len);
  if (len > 0 && !sendAll(aux(r)->fd, buf, len)) return ESP_FAIL;
  return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len) {
  if (len == HTTPD_RESP_USE_STRLEN) len = buf ? (ssize_t)strlen(buf) : 0;
  if (!aux(r)->sentHead) sendHead(r, -1);
  char sz[24];
  if (!buf) return sendAll(aux(r)->fd, "0\r\n\r\n", 5) ? ESP_OK : ESP_FAIL;
  if (len == 0) return ESP_OK;
  snprintf(sz, sizeof(sz), "%zx\r\n", (size_t)len);
  bool ok = sendAll(aux(r)->fd, sz, strlen(sz)) && sendAll(aux(r)->fd, buf, len) && sendAll(aux(r)->fd, "\r\n", 2);
  return ok ? ESP_OK : ESP_FAIL;
}

size_t httpd_req_get_url_query_len(httpd_req_t *r) {
  const char *q = strchr(r->uri, '?');
  return q ? strlen(q + 1) : 0;
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t n) {
  const char *q = strchr(r->uri, '?');
  if (!q) return ESP_ERR_NOT_FOUND;
  strncpy(buf, q + 1, n - 1);
  buf[n - 1] = 0;
  return strlen(q + 1) >= n ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
}

esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t n) {
  size_t kl = strlen(key);
  for (const char *p = qry; p && *p;) {
    const char *amp = strchr(p, '&');
    size_t seg = amp ? (size_t)(amp - p) : strlen(p);
    if (seg >= kl && strncmp(p, key, kl) == 0 && (seg == kl || p[kl] == '=')) {
      const char *v = seg == kl ? p + kl : p + kl + 1;
      size_t vl = seg - (v - p);
      size_t c = std::min(vl, n - 1);
      memcpy(val, v, c);
      val[c] = 0;
      return vl >= n ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
    }
    p = amp ? amp + 1 : nullptr;
  }
  return ESP_ERR_NOT_FOUND;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *f) {
  for (auto &kv : aux(r)->hdrs) if (ieq(kv.first, f)) return kv.second.size();
  return 0;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *f, char *val, size_t n) {
  for (auto &kv : aux(r)->hdrs) {
    if (!ieq(kv.first, f)) continue;
    strncpy(val, kv.second.c_str(), n - 1);
    val[n - 1] = 0;
    return kv.second.size() >= n ? ESP_ERR_HTTPD_RESULT_TRUNC : ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t n) {
  Aux *a = aux(r);
  if (!a->bodyLeft) return 0;
  int k = readSome(a->s, a->fd, buf, std::min(n, a->bodyLeft), a->s->cfg.recv_wait_timeout);
  if (k > 0) a->bodyLeft -= k;
  return k;
}

int httpd_send(httpd_req_t *r, const char *buf, size_t n) {
  pollfd pf{aux(r)->fd, POLLOUT, 0};
  if (poll(&pf, 1, aux(r)->s->cfg.send_wait_timeout * 1000) <= 0) return HTTPD_SOCK_ERR_TIMEOUT;
  ssize_t k = send(aux(r)->fd, buf, n, MSG_NOSIGNAL);
  return k < 0 ? HTTPD_SOCK_ERR_FAIL : (int)k;
}

esp_err_t httpd_req_async_handler_begin(httpd_req_t *r, httpd_req_t **out) {
  Aux *a = aux(r);
  auto *copyAux = new Aux(*a);
  auto *copy = new httpd_req_t(*r);
  copy->aux = copyAux;
  a->async = true;
  std::lock_guard<std::mutex> l(a->s->mu);
  a->s->conns[a->fd].busy = true;
  *out = copy;
  return ESP_OK;
}

esp_err_t httpd_req_async_handler_complete(httpd_req_t *r) {
  Aux *a = aux(r);
  Server *s = a->s;
  {
    std::lock_guard<std::mutex> l(s->mu);
    auto it = s->conns.find(a->fd);
    if (it != s->conns.end()) it->second.busy = false;
  }
  if (write(s->wake[1], "x", 1) < 0) {}
  delete a;
  delete r;
  return ESP_OK;
}

int httpd_req_to_sockfd(httpd_req_t *r) { return aux(r)->fd; }

esp_err_t httpd_sess_trigger_close(httpd_handle_t, int fd) {
  shutdown(fd, SHUT_RDWR);      // the loop sees the hang-up and drops the session
  return ESP_OK;
}
