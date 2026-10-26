#define VIEWBERNETES_TEST
#include "../src/viewbernetes.c"
#include <assert.h>
int main(void) {
    assert(fabs(quantity("250m")-.25)<1e-9);
    assert(fabs(quantity("123456789n")-.123456789)<1e-9);
    assert(quantity("2Gi")==2147483648.);
    assert(quantity("1000u")==.001);
    assert(quantity("1e3")==1000.);
    assert(quantity("1.5G")==1500000000.);
    assert(isnan(quantity("")) && isnan(quantity("oops")) && isnan(quantity("1MB")));
    assert(isnan(quantity("-1")) && isnan(quantity("inf")));
    Json j; char valid[]="{\"a\": [1, {\"b\": \"text\"}], \"c\": \"42m\"}";
    assert(json_init(&j,valid)); assert(j.t[field(&j,1,"a")].count==2);
    assert(fabs(amount(&j,1,"c")-.042)<1e-9); free(j.t);
    const char *bad[]={"{} garbage","{\"a\":1,}","{\"a\":01}","{\"a\":\"\\q\"}","{\"a\": [}","{\"a\":nope}","[]"};
    for(size_t k=0;k<sizeof(bad)/sizeof(*bad);k++) { char *s=strdup(bad[k]); assert(!json_init(&j,s)); free(j.t); free(s); }
    puts("quantity and JSON parser checks passed");
}
