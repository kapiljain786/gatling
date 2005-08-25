#include <io.h>
#include <byte.h>
#include <str.h>
#include <fmt.h>
#include <scan.h>
#include <socket.h>
#include <errmsg.h>
#include <dns.h>
#include <ip6.h>
#include <textcode.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

void usage() {
  die(0,"usage: bench [-n requests] [-c concurrency] [-t timeout] [-k] [http://]host[:port]/uri");
}

static int make_connection(char* ip,uint16 port,uint32 scope_id,int s) {
  int v6=byte_diff(ip,12,V4mappedprefix);
  if (v6) {
    if (s==-1) {
      s=socket_tcp6();
      if (s==-1) return -1;
      io_nonblock(s);
    }
    if (socket_connect6(s,ip,port,scope_id)==-1) {
      if (errno==EAGAIN || errno==EINPROGRESS || errno==EISCONN)
	return s;
      carpsys("socket_connect6");
      close(s);
      return -1;
    }
  } else {
    if (s==-1) {
      s=socket_tcp4();
      if (s==-1) return -1;
      io_nonblock(s);
    }
    if (socket_connect4(s,ip+12,port)==-1) {
      if (errno==EAGAIN || errno==EINPROGRESS || errno==EISCONN)
	return s;
      carpsys("socket_connect4");
      close(s);
      return -1;
    }
  }
  return s;
}


int main(int argc,char* argv[]) {
  char server[1024];
  int* fds;
  int* avail;
  long long* expected;
  unsigned long n=10000;	/* requests */
  unsigned long c=10;		/* concurrency */
  unsigned long t=0;		/* time limit in seconds */
  unsigned long k=0;		/* keep-alive */
  unsigned long long errors=0;
  unsigned long long bytes=0;
  int v=0;
  unsigned long i,done;
  uint16 port=80;
  uint32 scope_id=0;
  stralloc ips={0};
  char* request;
  unsigned long rlen;
  tai6464 first,now,next,last;

  server[0]=0;

  errmsg_iam("bench");
  signal(SIGPIPE,SIG_IGN);

  for (;;) {
    int i;
    int ch=getopt(argc,argv,"n:c:t:kv");
    if (ch==-1) break;
    switch (ch) {
    case 'n':
      i=scan_ulong(optarg,&n);
      if (i==0) die(1,"could not parse -n argument \"",optarg,"\".\n");
      break;
    case 'c':
      i=scan_ulong(optarg,&c);
      if (i==0) die(1,"could not parse -c argument \"",optarg,"\".\n");
      break;
    case 't':
      i=scan_ulong(optarg,&t);
      if (i==0) die(1,"could not parse -t argument \"",optarg,"\".\n");
      break;
    case 'k':
      k=1;
      break;
    case 'v':
      v=1;
      break;
    case '?':
      break;
    default:
      usage();
    }
  }
  if (n<1 || c<1 || !argv[optind]) usage();

  {
    char* host=argv[optind];
    int colon;
    int slash;
    char* c;
    if (byte_equal(host,7,"http://")) host+=7;
    colon=str_chr(host,':');
    slash=str_chr(host,'/');
    if (host[0]=='[') {	/* ipv6 IP notation */
      int tmp;
      ++host;
      --colon; --slash;
      tmp=str_chr(host,']');
      if (host[tmp]==']') host[tmp]=0;
      if (host[tmp+1]==':') colon=tmp+1;
      if (colon<tmp+1) colon=tmp+1+str_len(host+tmp+1);
    }
    if (colon<slash) {
      host[colon]=0;
      c=host+colon+1;
      if (c[scan_ushort(c,&port)]!='/') usage();
      *c=0;
    }
    host[colon]=0;
    c=host+slash;
    *c=0;
    {
      char* tmp=alloca(str_len(host)+1);
      tmp[fmt_str(tmp,host)]=0;
      host=tmp;
    }
    *c='/';
    {
      int tmp=str_chr(host,'%');
      if (host[tmp]) {
	host[tmp]=0;
	scope_id=socket_getifidx(host+tmp+1);
	if (scope_id==0)
	  carp("warning: network interface \"",host+tmp+1,"\" not found.");
      }
    }

    {
      stralloc a={0};
      stralloc_copys(&a,host);
      if (dns_ip6(&ips,&a)==-1)
	die(1,"could not find IP for \"",host,"\"!");
    }

    request=alloca(300+str_len(host)+3*str_len(c));
    {
      int i;
      i=fmt_str(request,"GET ");
      i+=fmt_urlencoded(request+i,c,str_len(c));
      i+=fmt_str(request+i," HTTP/1.0\r\nHost: ");
      i+=fmt_str(request+i,host);
      i+=fmt_str(request+i,":");
      i+=fmt_ulong(request+i,port);
      i+=fmt_str(request+i,"\r\nUser-Agent: bench/1.0\r\nConnection: ");
      i+=fmt_str(request+i,k?"keep-alive":"close");
      i+=fmt_str(request+i,"\r\n\r\n");
      rlen=i; request[rlen]=0;
    }

  }
  fds=alloca(c*sizeof(*fds));
  avail=alloca(c*sizeof(*avail));
  expected=alloca(c*sizeof(*expected));
  last.sec.x=23;
  for (i=0; i<c; ++i) { fds[i]=-1; avail[i]=1; }

  taia_now(&first);

  for (done=0; done<n; ) {

    if (t) {
      /* calculate timeout */
      taia_now(&now);
      if (now.sec.x != last.sec.x) {
	byte_copy(&last,sizeof(now),&now);
	byte_copy(&next,sizeof(now),&now);
	next.sec.x += t;
	while ((i=io_timeouted())!=-1) {
	  unsigned long j;
	  char numbuf[FMT_ULONG];
	  numbuf[fmt_ulong(numbuf,i)]=0;
	  carp("timeout on fd ",numbuf,"!");
	  j=(unsigned long)io_getcookie(i);
	  io_close(i);
	  avail[j]=1;
	  fds[j]=-1;
	}
      }
    }

    /* first, fill available connections */
    for (i=0; i<c; ++i)
      if (avail[i]==1 && fds[i]==-1) {
	fds[i]=make_connection(ips.s,port,scope_id,-1);
	if (fds[i]==-1) diesys(1,"socket/connect");
	avail[i]=2;
	if (io_fd(fds[i])==0) diesys(1,"io_fd");
	io_setcookie(fds[i],(void*)i);
//	io_wantread(fds[i]);
	io_wantwrite(fds[i]);
      }

    if (t)
      io_waituntil(next);
    else
      io_wait();

    /* second, see if we can write on a connection */
    while ((i=io_canwrite())!=-1) {
      int j;
      j=(unsigned long)io_getcookie(i);
      if (avail[j]==2) {
	if (make_connection(ips.s,port,scope_id,i)==-1) {
	  ++errors;
	  if (v) write(1,"!",1);
	  io_close(i);
	  avail[j]=1;
	  fds[j]=-1;
	  continue;
	}
      }
      if (io_trywrite(i,request,rlen)!=rlen) {
	++errors;
	if (v) write(1,"-",1);
	io_close(i);
	avail[j]=1;
	fds[j]=-1;
	continue;
      }
      io_dontwantwrite(i);
      io_wantread(i);
      expected[j]=-1;
      if (v) write(1,"+",1);
    }

    /* third, see if we got served */
    while ((i=io_canread())!=-1) {
      char buf[8193];
      int l,j;
      buf[8192]=0;
      j=(unsigned long)io_getcookie(i);
      if ((l=io_tryread(i,buf,sizeof(buf)-1))<=0) {
	if (l==0) { /* EOF.  Mhh. */
	  if (expected[j]>0) {
	    ++errors;
	    if (v) write(1,"-",1);	/* so whine a little */
	  }
	  if (expected[j]==-2)
	    ++done;
	  io_close(i);
	  avail[j]=1;
	  fds[j]=-1;
	} else if (l==-3) {
	  ++errors;
	  if (v) write(1,"!",1);
	  carpsys("read");
	}
      } else {
	bytes+=l;
	if (v) write(1,".",1);
	/* read something */
	if (expected[j]==-1) {	/* expecting header */
	  int k;
	  /* OK, so this is a very simplistic header parser.  No
	   * buffering.  At all.  We expect the Content-Length header to
	   * come in one piece. */
	  expected[j]=-2;
	  if (!done) {
	    for (k=0; k<l; ++k)
	      if (str_start(buf+k,"\nServer: ")) {
		char* tmp=buf+(k+=9);
		for (; k<l; ++k)
		  if (buf[k]=='\r') break;
		k=buf+k-tmp;
		if (k>sizeof(server)-1) k=sizeof(server)-1;
		byte_copy(server,k,tmp);
		server[k]=0;
		break;
	      }
	  }
	  for (k=0; k<l; ++k) {
	    if (str_start(buf+k,"\nContent-Length: ")) {
	      k+=17;
	      if (buf[k+scan_ulonglong(buf+k,expected+j)] != '\r')
		die(1,"parse error in HTTP header!");
	    } else if (str_start(buf+k,"\r\n\r\n"))
	      break;
	  }
	  if (expected[j]>0) {
	    if (l-(k+4)>expected[j])
	      expected[j]=0;
	    else
	      expected[j]-=l-(k+4);
	  }
	} else if (expected[j]==-2) {
	  /* no content-length header, eat everything until EOF */
	} else {
	  if (l>expected[j]) {
	    carp("got more than expected!");
	    expected[j]=0;
	  } else
	    expected[j]-=l;
	}
	if (expected[j]==0) {
	  ++done;	/* one down! */
	  avail[j]=1;
	  if (k) {
	    io_dontwantread(i);
	    io_wantwrite(i);
	    expected[j]=0;
	  } else {
	    io_close(i);
	    fds[i]=-1;
	  }
	}
      }
    }
  }

  taia_now(&now);
  taia_sub(&now,&now,&first);
  {
    char a[FMT_ULONG];
    char b[FMT_ULONG];
    char c[FMT_ULONG];
    char d[FMT_ULONG];
    unsigned long long l;
    a[fmt_ulong(a,now.sec.x)]=0;
    b[fmt_ulong0(b,(now.nano%1000000000)/1000,6)]=0;
    c[fmt_ulong(c,done)]=0;
    d[fmt_ulonglong(d,errors)]=0;
    if (server[0]) carp("Server: ",server);
    carp(c," requests, ",d," errors.");
    c[fmt_ulonglong(c,bytes)]=0;
    carp(c," bytes in ",a,".",b," seconds.");

    /* let's say bytes = 10 MB, time = 1.2 sec.
     * then we want 10*1024*1024/1.2 == 8 MB/sec */

    l = (now.sec.x * 1024) + now.nano/976562;
    if (l) {
      l = bytes / l;
      b[fmt_humank(b,l*1024)]=0;
      carp("Throughput: ",b,"iB/sec");
    } else
      carp("Need longer test to calculate throughput.");


    l = (now.sec.x * 1000) + now.nano/1000000;
    l = (done*10000) / l;
    a[fmt_ulong(a,l/10)]=0;
    carp("Requests per second: ",a);
  }

  return 0;
}