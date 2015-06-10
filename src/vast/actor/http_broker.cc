#include <iostream>
#include <chrono>

#include "caf/all.hpp"
#include "caf/io/all.hpp"
#include "vast/logger.h"
#include "vast/actor/http_broker.h"

#include "vast/actor/actor.h"
#include "vast/event.h"
#include "vast/time.h"
#include "vast/uuid.h"
#include "vast/util/json.h"


// FIXME: remove after debugging
using std::cout;
using std::cerr;
using std::endl;

using namespace caf;
using namespace caf::io;
using namespace std::string_literals;

namespace vast {

const char HEX2DEC[256] =
{
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 1 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 2 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,

    /* 4 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 5 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 6 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 7 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* 8 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 9 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* A */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* B */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* C */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* D */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* E */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* F */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

std::string UriDecode(const std::string & sSrc)
{
    // Note from RFC1630:  "Sequences which start with a percent sign
    // but are not followed by two hexadecimal characters (0-9, A-F) are reserved
    // for future extension"

    const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
	const int SRC_LEN = sSrc.length();
    const unsigned char * const SRC_END = pSrc + SRC_LEN;
    const unsigned char * const SRC_LAST_DEC = SRC_END - 2;   // last decodable '%'

    char * const pStart = new char[SRC_LEN];
    char * pEnd = pStart;

    while (pSrc < SRC_LAST_DEC)
	{
		if (*pSrc == '%')
        {
            char dec1, dec2;
            if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
                && -1 != (dec2 = HEX2DEC[*(pSrc + 2)]))
            {
                *pEnd++ = (dec1 << 4) + dec2;
                pSrc += 3;
                continue;
            }
        }

        *pEnd++ = *pSrc++;
	}

    // the last 2- chars
    while (pSrc < SRC_END)
        *pEnd++ = *pSrc++;

    std::string sResult(pStart, pEnd);
    delete [] pStart;
	return sResult;
}


//using tick_atom = atom_constant<atom("tick")>;

constexpr const char http_ok[] = R"__(HTTP/1.1 200 OK
Content-Type: text/plain
Connection: keep-alive
Transfer-Encoding: chunked

d
Hi there! :)

0


)__";

constexpr const char http_header[] = R"__(HTTP/1.1 200 OK
Content-Type: application/json
Connection: keep-alive

)__";

bool first_event_ = true;

template <size_t Size>
constexpr size_t cstr_size(const char (&)[Size])
{
  return Size;
}


std::string parse_url(new_data_msg msg)
{
  std::string bufstr(msg.buf.begin(), msg.buf.end());
  //aout(self) << bufstr << endl;
  auto pos = bufstr.find(' ');
  auto url = bufstr.substr(pos + 1, bufstr.find(' ', pos+1)-4);
  //aout(self) << "url:'" << url << "'" << endl;
  return url;
}

std::string create_response(std::string const& content)
{
  auto response = ""s;
  response += "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: application/json\r\n";
  response += "\r\n";
  response += content;
  response += "\r\n";
  return response;
}

void send_query(broker* self, std::string query, actor const& node){
  caf::message_builder mb;
  mb.append("spawn");
  mb.append("exporter");
  mb.append("-h");
  mb.append(query);
  self->send(node, mb.to_message());

  caf::message_builder mb1;
  mb1.append("connect");
  mb1.append("exporter");
  mb1.append("archive");
  self->send(node, mb1.to_message());

  caf::message_builder mb2;
  mb2.append("connect");
  mb2.append("exporter");
  mb2.append("index");
  self->send(node, mb2.to_message());

  //TODO: connect exporter to sink

  caf::message_builder mb3;
  mb3.append("send");
  mb3.append("exporter");
  mb3.append("run");
  self->send(node, mb3.to_message());

}

bool handle(event const& e, broker* self, connection_handle hdl)
{
  auto j = to<util::json>(e);
  if (! j)
    return false;
  auto content = to_string(*j, true);
  content += "\r\n";

  if (first_event_){
    first_event_ = false;
    auto ans = create_response("");
    self->write(hdl, ans.size(), ans.c_str());
  }

  self->write(hdl, content.size(), content.c_str());
  return true;
}

behavior connection_worker(broker* self, connection_handle hdl, actor const& node)
{
  self->configure_read(hdl, receive_policy::at_most(1024));

  return
  {
    [=](new_data_msg const& msg)
    {
      VAST_DEBUG(self, "got", msg.buf.size(), "bytes");
      auto url = parse_url(msg);
      auto query = url.substr(url.find("query=") + 6, url.size());
      query = UriDecode(query);
      VAST_DEBUG(self, "got", query, "as query");

      send_query(self, query, node);

      auto content ="{query : \""s;
      content.append(query);
      content.append("\"}");

      auto ans = create_response(content);
      VAST_DEBUG(self, "responding with", ans);
      self->write(msg.handle, ans.size(), ans.c_str());
      self->quit();
    },
    [=](connection_closed_msg const&)
    {
      self->quit();
    },
    // handle sink messages
    [=](exit_msg const& msg)
    {
      self->quit(msg.reason);
    },
    [=](uuid const&, event const& e)
    {
      handle(e, self, hdl);
    },
    [=](uuid const&, std::vector<event> const& v)
    {
      assert(! v.empty());
      for (auto& e : v)
        if (! handle(e, self, hdl))
          return;
    },
    [=](uuid const& id, done_atom, time::extent runtime)
    {
      VAST_VERBOSE(self, "got DONE from query", id << ", took", runtime);
      self->quit(exit::done);
    }
  };
}

behavior http_broker_function(broker* self, actor const& node)
{
  VAST_VERBOSE("http_broker_function called");
  return
  {
    [=](new_connection_msg const& ncm)
    {
      VAST_DEBUG(self, "got new connection");
      auto worker = self->fork(connection_worker, ncm.handle, node);
      self->monitor(worker);
      self->link_to(worker);
    },
    others >> [=]
    {
      auto msg = to_string(self->current_message());
      VAST_WARN(self, "got unexpected msg:", msg);
      aout(self) << "unexpected: " << msg << endl;
    }
  };
}

optional<uint16_t> as_u16(std::string const& str)
{
  return static_cast<uint16_t>(stoul(str));
}

} // namespace vast