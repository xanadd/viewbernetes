#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <ncurses.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define MAX_ITEMS 16384
#define MAX_JSON (32 * 1024 * 1024)
extern char **environ;
typedef struct { int start, end, next, count; char type; } Token;
typedef struct { char *s; Token *t; int len, pos, n, cap; } Json;
static void *allocate(size_t n) { void *p = calloc(1, n); if (!p) { fputs("Out of memory\n", stderr); exit(1); } return p; }
static void ws(Json *j) { while (j->pos < j->len && isspace((unsigned char)j->s[j->pos])) j->pos++; }
static int parse_value(Json *j, int depth) {
    ws(j);
    if (depth > 64 || j->pos >= j->len || j->n >= j->cap) return -1;
    int i = ++j->n; Token *t = &j->t[i];
    t->start = j->pos; t->type = j->s[j->pos++];
    if (t->type == '{' || t->type == '[') {
        char close = t->type == '{' ? '}' : ']'; ws(j);
        if (j->pos < j->len && j->s[j->pos] != close) {
            for (;;) {
                if (t->type == '{') {
                    ws(j); if (j->pos >= j->len || j->s[j->pos] != '"' || parse_value(j, depth+1) < 0) return -1;
                    ws(j); if (j->pos >= j->len || j->s[j->pos++] != ':') return -1;
                }
                if (parse_value(j, depth+1) < 0) return -1;
                t->count++; ws(j);
                if (j->pos >= j->len) return -1;
                if (j->s[j->pos] != ',') break;
                j->pos++;
            }
        }
        if (j->pos >= j->len || j->s[j->pos++] != close) return -1;
    } else if (t->type == '"') {
        bool closed = false;
        while (j->pos < j->len) {
            unsigned char c = (unsigned char)j->s[j->pos++];
            if (c == '"') { closed = true; break; }
            if (c < 32) return -1;
            if (c == '\\') {
                if (j->pos >= j->len) return -1;
                c = (unsigned char)j->s[j->pos++];
                if (c == 'u') { for (int k=0; k<4; k++) if (j->pos >= j->len || !isxdigit((unsigned char)j->s[j->pos++])) return -1; }
                else if (!strchr("\"\\/bfnrt", c)) return -1;
            }
        }
        if (!closed) return -1;
    } else {
        while (j->pos < j->len && !strchr(" \t\r\n,]}", j->s[j->pos])) j->pos++;
        int n = j->pos - t->start;
        const char *s = j->s + t->start;
        if (!((n==4 && !memcmp(s,"true",4)) || (n==5 && !memcmp(s,"false",5)) || (n==4 && !memcmp(s,"null",4)))) {
            int p=t->start;
            if (j->s[p]=='-') p++;
            if (p>=j->pos || !isdigit((unsigned char)j->s[p])) return -1;
            if (j->s[p]=='0') p++; else while (p<j->pos && isdigit((unsigned char)j->s[p])) p++;
            if (p<j->pos && j->s[p]=='.') { p++; int start=p; while (p<j->pos && isdigit((unsigned char)j->s[p])) p++; if(p==start) return -1; }
            if (p<j->pos && (j->s[p]=='e'||j->s[p]=='E')) { p++; if(p<j->pos && (j->s[p]=='+'||j->s[p]=='-')) p++; int start=p; while(p<j->pos && isdigit((unsigned char)j->s[p])) p++; if(p==start) return -1; }
            if (p!=j->pos) return -1;
        }
    }
    t->end=j->pos; t->next=j->n+1; return i;
}
static bool json_init(Json *j, char *s) {
    memset(j,0,sizeof(*j)); j->s=s; j->len=(int)strlen(s); j->cap=j->len/2+2;
    j->t=allocate(((size_t)j->cap+1)*sizeof(Token));
    if (parse_value(j,0)<0) return false;
    ws(j); return j->pos==j->len && j->t[1].type=='{';
}
static bool eq(Json *j,int i,const char *s) { return i>0 && j->t[i].type=='"' && j->t[i].end-j->t[i].start-2==(int)strlen(s) && !memcmp(j->s+j->t[i].start+1,s,strlen(s)); }
static int field(Json *j,int i,const char *s) {
    if(i<=0 || j->t[i].type!='{') return 0;
    for(int k=i+1;k<j->t[i].next;) { int v=k+1; if(eq(j,k,s)) return v; k=j->t[v].next; } return 0;
}
static void strval(Json *j,int i,char *out,size_t size) {
    if(!size) return;
    out[0]=0; if(i<=0 || j->t[i].type!='"') return;
    int a=j->t[i].start+1,b=j->t[i].end-1; size_t n=0;
    for(int k=a;k<b && n+1<size;k++) { unsigned char c=(unsigned char)j->s[k]; out[n++]=(c>=32 && c<127)?(char)c:'?'; } out[n]=0;
}
static double quantity(const char *s) {
    char *end; errno=0; double v=strtod(s,&end);
    if(end==s || errno || !isfinite(v) || v<0) return NAN;
    const char *units[]={"","n","u","m","k","K","M","G","T","P","E","Ki","Mi","Gi","Ti","Pi","Ei"};
    const double factors[]={1,1e-9,1e-6,1e-3,1e3,1e3,1e6,1e9,1e12,1e15,1e18,1024,1048576,1073741824.,1099511627776.,1125899906842624.,1152921504606846976.};
    for(size_t i=0;i<sizeof(factors)/sizeof(*factors);i++) if(!strcmp(end,units[i])) { double r=v*factors[i]; return isfinite(r)?r:NAN; }
    return NAN;
}
static double amount(Json *j,int i,const char *key) { char s[80]; strval(j,field(j,i,key),s,sizeof(s)); return quantity(s); }
typedef struct { char name[256]; double cores,ram,cpu,mem; bool measured,ready; } Node;
typedef struct { char name[256],ns[256]; double cpu,mem; } Pod;
typedef struct { Node *nodes; Pod *pods; int nn,np,measured; double cpu,mem,cores,ram; char error[256],stamp[64]; time_t updated; } Snapshot;
static void release(Snapshot *s) { free(s->nodes); free(s->pods); memset(s,0,sizeof(*s)); }
static char context[256];
static const char *namespace_filter=NULL;
static atomic_bool stopping=false, refresh_requested=false;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Snapshot current;
static int interval=2;
static bool demo=false;
static long long millis(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
/* No shell, no kubeconfig reads, and no kubectl stderr in diagnostics. */
static char *kubectl(const char *const args[]) {
    int fds[2]; if(pipe(fds)) return NULL;
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    posix_spawn_file_actions_addclose(&actions,fds[0]); posix_spawn_file_actions_addclose(&actions,fds[1]);
    char *argv[24]; int n=0; argv[n++]="kubectl"; argv[n++]="--request-timeout=8s";
    if(context[0]) { argv[n++]="--context"; argv[n++]=context; }
    for(int i=0;args[i] && n<22;i++) argv[n++]=(char *)args[i];
    argv[n]=NULL; pid_t pid;
    int rc=posix_spawnp(&pid,"kubectl",&actions,NULL,argv,environ);
    posix_spawn_file_actions_destroy(&actions); close(fds[1]);
    if(rc) { close(fds[0]); return NULL; }
    fcntl(fds[0],F_SETFL,O_NONBLOCK);
    size_t cap=8192,len=0; char *buf=allocate(cap); long long deadline=millis()+10000;
    bool ok=true, eof=false; int status=0; bool exited=false;
    while(!eof || !exited) {
        if(atomic_load(&stopping) || millis()>deadline) { ok=false; break; }
        struct pollfd p={fds[0],POLLIN,0}; poll(&p,1,50);
        if(!eof) for(;;) {
            char chunk[8192]; ssize_t got=read(fds[0],chunk,sizeof(chunk));
            if(got==0) { eof=true; break; }
            if(got<0) { if(errno!=EAGAIN && errno!=EINTR) ok=false; break; }
            if(len+(size_t)got+1>MAX_JSON) { ok=false; break; }
            if(len+(size_t)got+1>cap) { cap*=2; char *b=realloc(buf,cap); if(!b) { ok=false; break; } buf=b; }
            memcpy(buf+len,chunk,(size_t)got); len+=(size_t)got;
        }
        if(!ok) break;
        if(!exited) { pid_t w=waitpid(pid,&status,WNOHANG); if(w==pid) exited=true; else if(w<0 && errno!=EINTR) { ok=false; break; } }
    }
    close(fds[0]);
    if(!exited) { kill(pid,SIGKILL); while(waitpid(pid,&status,0)<0 && errno==EINTR) {} }
    if(!ok || !WIFEXITED(status) || WEXITSTATUS(status)) { free(buf); return NULL; }
    buf[len]=0; return buf;
}
static bool fetch(Json *j,const char *const args[]) { char *s=kubectl(args); memset(j,0,sizeof(*j)); if(!s) return false; return json_init(j,s); }
static void json_free(Json *j) { free(j->s); free(j->t); }
static bool load(Snapshot *s) {
    s->nodes=allocate(MAX_ITEMS*sizeof(Node)); s->pods=allocate(MAX_ITEMS*sizeof(Pod));
    Json j;
    if(!fetch(&j,(const char *const[]){"get","nodes","-o","json",NULL})) { json_free(&j); snprintf(s->error,sizeof(s->error),"Cannot read nodes: check kubectl, context, connectivity and RBAC."); return false; }
    int list=field(&j,1,"items");
    if(!list || j.t[list].type!='[' || j.t[list].count>MAX_ITEMS) { json_free(&j); snprintf(s->error,sizeof(s->error),"Invalid or oversized node list."); return false; }
    for(int i=list+1;i<j.t[list].next;i=j.t[i].next) {
        Node *n=&s->nodes[s->nn++]; strval(&j,field(&j,field(&j,i,"metadata"),"name"),n->name,sizeof(n->name));
        int status=field(&j,i,"status"),capacity=field(&j,status,"capacity");
        n->cores=amount(&j,capacity,"cpu"); n->ram=amount(&j,capacity,"memory");
        if(!n->name[0] || !isfinite(n->cores) || !isfinite(n->ram) || n->cores<=0 || n->ram<=0) { json_free(&j); snprintf(s->error,sizeof(s->error),"Invalid node capacity."); return false; }
        int conditions=field(&j,status,"conditions");
        if(conditions && j.t[conditions].type=='[') for(int k=conditions+1;k<j.t[conditions].next;k=j.t[k].next) if(eq(&j,field(&j,k,"type"),"Ready")) n->ready=eq(&j,field(&j,k,"status"),"True");
    }
    json_free(&j);
    if(!fetch(&j,(const char *const[]){"get","--raw","/apis/metrics.k8s.io/v1beta1/nodes",NULL})) { json_free(&j); snprintf(s->error,sizeof(s->error),"Node metrics unavailable: metrics-server and metrics.k8s.io read access required."); return false; }
    list=field(&j,1,"items");
    if(!list || j.t[list].type!='[') { json_free(&j); snprintf(s->error,sizeof(s->error),"Invalid node metrics response."); return false; }
    for(int i=list+1;i<j.t[list].next;i=j.t[i].next) {
        char name[256]; strval(&j,field(&j,field(&j,i,"metadata"),"name"),name,sizeof(name));
        int usage=field(&j,i,"usage"); double cpu=amount(&j,usage,"cpu"),mem=amount(&j,usage,"memory");
        for(int k=0;k<s->nn;k++) if(!strcmp(s->nodes[k].name,name) && !s->nodes[k].measured && isfinite(cpu) && isfinite(mem)) {
            Node *n=&s->nodes[k]; n->cpu=cpu; n->mem=mem; n->measured=true; s->measured++;
            s->cpu+=cpu; s->mem+=mem; s->cores+=n->cores; s->ram+=n->ram;
            if(!s->stamp[0]) strval(&j,field(&j,i,"timestamp"),s->stamp,sizeof(s->stamp));
        }
    }
    json_free(&j);
    char path[512];
    if(namespace_filter) snprintf(path,sizeof(path),"/apis/metrics.k8s.io/v1beta1/namespaces/%s/pods",namespace_filter);
    else snprintf(path,sizeof(path),"/apis/metrics.k8s.io/v1beta1/pods");
    if(!fetch(&j,(const char *const[]){"get","--raw",path,NULL})) { json_free(&j); snprintf(s->error,sizeof(s->error),"Pod metrics unavailable: check metrics-server and pod metrics RBAC."); s->updated=time(NULL); return true; }
    list=field(&j,1,"items");
    if(!list || j.t[list].type!='[' || j.t[list].count>MAX_ITEMS) { json_free(&j); snprintf(s->error,sizeof(s->error),"Invalid or oversized pod metrics list."); s->updated=time(NULL); return true; }
    for(int i=list+1;i<j.t[list].next;i=j.t[i].next) {
        Pod p={0}; int meta=field(&j,i,"metadata");
        strval(&j,field(&j,meta,"name"),p.name,sizeof(p.name)); strval(&j,field(&j,meta,"namespace"),p.ns,sizeof(p.ns));
        int containers=field(&j,i,"containers"); bool valid=containers && j.t[containers].type=='[' && j.t[containers].count>0;
        if(valid) for(int k=containers+1;k<j.t[containers].next;k=j.t[k].next) {
            int usage=field(&j,k,"usage"); double cpu=amount(&j,usage,"cpu"),mem=amount(&j,usage,"memory");
            if(!isfinite(cpu)||!isfinite(mem)) { valid=false; break; } p.cpu+=cpu; p.mem+=mem;
        }
        if(valid && p.name[0] && p.ns[0]) s->pods[s->np++]=p;
    }
    json_free(&j); s->updated=time(NULL); return true;
}
static void load_demo(Snapshot *s) {
    s->nodes=allocate(3*sizeof(Node)); s->pods=allocate(12*sizeof(Pod)); s->nn=s->measured=3; s->np=12;
    for(int i=0;i<3;i++) { Node *n=&s->nodes[i]; snprintf(n->name,sizeof(n->name),"worker-%02d",i+1); n->cores=8; n->ram=32.*1073741824.; n->cpu=2.1+i*1.7; n->mem=(9.+i*6)*1073741824.; n->ready=n->measured=true; s->cpu+=n->cpu; s->mem+=n->mem; s->cores+=n->cores; s->ram+=n->ram; }
    for(int i=0;i<12;i++) { Pod *p=&s->pods[i]; snprintf(p->name,sizeof(p->name),"%s-7c9b4d-%04d",i%3==0?"api":i%3==1?"worker":"cache",i); snprintf(p->ns,sizeof(p->ns),"%s",i%4?"production":"kube-system"); p->cpu=2.4/(i+1); p->mem=(1800.-i*120)*1048576.; }
    s->updated=time(NULL); snprintf(s->stamp,sizeof(s->stamp),"simulated metrics");
}
static int sort_memory=0;
static int compare(const void *a,const void *b) { const Pod *x=a,*y=b; double d=sort_memory?y->mem-x->mem:y->cpu-x->cpu; return d>0?1:d<0?-1:strcmp(x->name,y->name); }
static double percent(double a,double b) { return b>0?a/b*100:0; }
static void print_snapshot(Snapshot *s) {
    printf("viewbernetes %s | context: %s%s\n",VERSION,context,demo?" [DEMO]":"");
    if(s->updated) {
        printf("Nodes measured: %d/%d | CPU %.2f/%.2f cores (%.1f%%) | Memory %.2f/%.2f GiB (%.1f%%)\n",s->measured,s->nn,s->cpu,s->cores,percent(s->cpu,s->cores),s->mem/1073741824.,s->ram/1073741824.,percent(s->mem,s->ram));
        printf("Metrics timestamp: %s | totals cover measured nodes only\n",s->stamp);
        for(int i=0;i<s->nn;i++) { Node *n=&s->nodes[i]; if(n->measured) printf("%-24s %-8s CPU %.1f%% (%.2f/%.2f cores) MEM %.1f%% (%.2f/%.2f GiB)\n",n->name,n->ready?"Ready":"NotReady",percent(n->cpu,n->cores),n->cpu,n->cores,percent(n->mem,n->ram),n->mem/1073741824.,n->ram/1073741824.); else printf("%-24s metrics unavailable\n",n->name); }
        puts("NAMESPACE                POD                                         CPU(m)    MEM(MiB)");
        qsort(s->pods,(size_t)s->np,sizeof(Pod),compare);
        for(int i=0;i<s->np;i++) printf("%-24s %-42s %8.0f %11.1f\n",s->pods[i].ns,s->pods[i].name,s->pods[i].cpu*1000,s->pods[i].mem/1048576.);
    }
    if(s->error[0]) printf("ERROR: %s\n",s->error);
}
static void *worker(void *unused) {
    (void)unused;
    while(!atomic_load(&stopping)) {
        Snapshot next={0}; if(demo) load_demo(&next); else load(&next);
        pthread_mutex_lock(&lock);
        if(next.updated || !current.updated) { release(&current); current=next; memset(&next,0,sizeof(next)); }
        else snprintf(current.error,sizeof(current.error),"%s",next.error);
        pthread_mutex_unlock(&lock); release(&next);
        for(int i=0;i<interval*10 && !atomic_load(&stopping);i++) { if(atomic_exchange(&refresh_requested,false)) break; struct timespec t={0,100000000}; nanosleep(&t,NULL); }
    }
    return NULL;
}
static void line(int y,int color,const char *s) { if(y<0 || y>=LINES || COLS<2) return; attron(COLOR_PAIR(color)); mvaddnstr(y,1,s,COLS-2); attroff(COLOR_PAIR(color)); }
static void meter(int y,int x,int width,const char *label,double pct,const char *detail,bool available) {
    if(width<18 || y>=LINES-2) return;
    char text[256]; snprintf(text,sizeof(text),"%s %s",label,detail);
    mvaddnstr(y,x,text,width);
    if(y+1>=LINES-2) return;
    int w=width-9; if(w>60) w=60;
    mvaddch(y+1,x,'[');
    int fill=available?(int)(fmin(100,fmax(0,pct))*w/100):0;
    attron(COLOR_PAIR(pct>=85?4:pct>=65?3:2));
    for(int k=0;k<w;k++) mvaddch(y+1,x+1+k,k<fill?'|':'.');
    attroff(COLOR_PAIR(pct>=85?4:pct>=65?3:2));
    snprintf(text,sizeof(text),available?"]%5.1f%%":"]   N/A",pct); mvaddnstr(y+1,x+w+1,text,7);
}
static void draw(Snapshot *s,int node_offset,int pod_offset) {
    erase(); char b[1024];
    if(LINES<18 || COLS<70) { line(0,3,"Minimum size: 70 columns x 18 rows."); line(2,1,"Resize the terminal; q quits. Use --once for plain output."); refresh(); return; }
    attron(A_BOLD); snprintf(b,sizeof(b)," viewbernetes %s  /  %s%s",VERSION,context,demo?"  [DEMO]":""); line(0,1,b); attroff(A_BOLD);
    snprintf(b,sizeof(b),"%d/%d nodes measured | %d pods | scope: %s | sort: %s | refresh: %ds",s->measured,s->nn,s->np,namespace_filter?namespace_filter:"all namespaces",sort_memory?"memory":"CPU",interval); line(1,0,b);
    int half=(COLS-4)/2;
    snprintf(b,sizeof(b),"%.2f / %.0f cores",s->cpu,s->cores); meter(3,2,half,"CLUSTER CPU",percent(s->cpu,s->cores),b,s->measured>0);
    snprintf(b,sizeof(b),"%.1f / %.1f GiB",s->mem/1073741824.,s->ram/1073741824.); meter(3,half+3,half,"MEMORY",percent(s->mem,s->ram),b,s->measured>0);
    int visible=(LINES-13)/3; if(visible<1) visible=1; if(visible>6) visible=6;
    snprintf(b,sizeof(b)," NODES  [%d-%d / %d]  capacity basis; missing metrics = N/A",s->nn?node_offset+1:0,s->nn<node_offset+visible?s->nn:node_offset+visible,s->nn); line(6,1,b);
    int y=7;
    for(int i=node_offset;i<s->nn && i<node_offset+visible;i++,y+=3) {
        Node *n=&s->nodes[i]; snprintf(b,sizeof(b),"%.24s %s %.2f/%.0f cores",n->name,n->ready?"+":"!",n->cpu,n->cores);
        meter(y,2,half,"CPU",percent(n->cpu,n->cores),b,n->measured);
        snprintf(b,sizeof(b),"%.1f/%.1f GiB",n->mem/1073741824.,n->ram/1073741824.); meter(y,half+3,half,"MEM",percent(n->mem,n->ram),b,n->measured);
    }
    if(!s->nn) { line(y,3,"Waiting for Kubernetes metrics..."); y+=2; }
    snprintf(b,sizeof(b)," %-18s %-*s %9s %10s","NAMESPACE",COLS-46,"POD","CPU m","MEM MiB"); line(y++,1,b);
    qsort(s->pods,(size_t)s->np,sizeof(Pod),compare);
    for(int i=pod_offset;i<s->np && y<LINES-3;i++,y++) { Pod *p=&s->pods[i]; snprintf(b,sizeof(b)," %-18.18s %-*.*s %9.0f %10.1f",p->ns,COLS-46,COLS-46,p->name,p->cpu*1000,p->mem/1048576.); line(y,i%2?0:2,b); }
    if(s->error[0]) { snprintf(b,sizeof(b),"%s%s",s->updated?"STALE / ":"",s->error); line(LINES-3,4,b); }
    else { snprintf(b,sizeof(b),"Sample: %s | fetched %lds ago | totals: measured nodes",s->stamp,s->updated?(long)(time(NULL)-s->updated):0L); line(LINES-3,0,b); }
    line(LINES-2,3,"c CPU  m memory  j/k pods  n/p nodes  r refresh  q quit");
    line(LINES-1,0,"CPU = aggregate cores; memory = working set; process list = pods."); refresh();
}
static volatile sig_atomic_t interrupted=0;
static void on_signal(int sig) { (void)sig; interrupted=1; }
static void usage(void) { puts("Usage: viewbernetes [--once] [--demo] [-n NAMESPACE] [-i SECONDS]\n\nNative Kubernetes cluster monitor. Uses the current kubectl context.\n  --once       Print a snapshot without a terminal\n  --demo       Simulated cluster; never calls kubectl\n  -n, --namespace NAME  Scope pod metrics (nodes remain cluster-wide)\n  -i, --interval N      Refresh every 1-3600 seconds (default 2)\n  --version    Print version\n\nKeys: c/m sort CPU/memory, j/k or arrows scroll pods, n/p scroll nodes,\n      r refresh, q quit. Requires kubectl and metrics-server."); }
#ifndef VIEWBERNETES_TEST
int main(int argc,char **argv) {
    bool once=false;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { usage(); return 0; }
        if(!strcmp(argv[i],"--version")) { puts(VERSION); return 0; }
        if(!strcmp(argv[i],"--once")) once=true;
        else if(!strcmp(argv[i],"--demo")) demo=true;
        else if((!strcmp(argv[i],"-n") || !strcmp(argv[i],"--namespace")) && i+1<argc) {
            namespace_filter=argv[++i]; size_t len=strlen(namespace_filter);
            if(!len || len>63 || !isalnum((unsigned char)namespace_filter[0]) || !isalnum((unsigned char)namespace_filter[len-1])) { fputs("Invalid namespace\n",stderr); return 2; }
            for(size_t k=0;k<len;k++) if(!(namespace_filter[k]>='a' && namespace_filter[k]<='z') && !isdigit((unsigned char)namespace_filter[k]) && namespace_filter[k]!='-') { fputs("Invalid namespace\n",stderr); return 2; }
        } else if((!strcmp(argv[i],"-i") || !strcmp(argv[i],"--interval")) && i+1<argc) {
            char *end; long v=strtol(argv[++i],&end,10); if(!argv[i][0] || *end || v<1 || v>3600) { fputs("Interval must be 1-3600 seconds\n",stderr); return 2; } interval=(int)v;
        } else { fputs("Unknown or incomplete option; use --help\n",stderr); return 2; }
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    if(!demo) {
        char *s=kubectl((const char *const[]){"config","current-context",NULL});
        if(!s || !s[0] || strlen(s)>=sizeof(context)) { free(s); fputs("Cannot determine current context. Check kubectl and select a context.\n",stderr); return 1; }
        s[strcspn(s,"\r\n")]=0; snprintf(context,sizeof(context),"%s",s); free(s);
        for(size_t k=0;context[k];k++) if((unsigned char)context[k]<32 || (unsigned char)context[k]>126) { fputs("Context name must contain printable ASCII characters.\n",stderr); return 1; }
    } else snprintf(context,sizeof(context),"demo-cluster");
    if(once) { Snapshot s={0}; bool ok=true; if(demo) load_demo(&s); else ok=load(&s); print_snapshot(&s); int rc=ok && !s.error[0]?0:1; release(&s); return rc; }
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)) { fputs("Interactive mode requires a terminal; use --once.\n",stderr); return 2; }
    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE); curs_set(0); timeout(100);
    if(has_colors()) { start_color(); use_default_colors(); init_pair(1,COLOR_CYAN,-1); init_pair(2,COLOR_GREEN,-1); init_pair(3,COLOR_YELLOW,-1); init_pair(4,COLOR_RED,-1); }
    pthread_t thread; if(pthread_create(&thread,NULL,worker,NULL)) { endwin(); fputs("Cannot start metrics worker\n",stderr); return 1; }
    int no=0,po=0;
    while(!interrupted) {
        pthread_mutex_lock(&lock); if(no>=current.nn) no=current.nn?current.nn-1:0; if(po>=current.np) po=current.np?current.np-1:0; draw(&current,no,po); pthread_mutex_unlock(&lock);
        int key=getch(); if(key=='q' || key=='Q') break;
        if(key=='c' || key=='m') { sort_memory=key=='m'; po=0; }
        if(key=='j'||key==KEY_DOWN) po++;
        if(key=='k'||key==KEY_UP) { if(po>0) po--; }
        if(key=='n') no++;
        if(key=='p' && no>0) no--;
        if(key=='r') atomic_store(&refresh_requested,true);
    }
    endwin(); atomic_store(&stopping,true); pthread_join(thread,NULL); release(&current); return 0;
}
#endif
