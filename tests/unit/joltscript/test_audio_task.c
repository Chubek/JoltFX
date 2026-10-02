#include "joltscript/audio_kernels.h"
#include "joltscript/audio_task.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2); jolt_program_t *program=NULL;
    assert(jolt_audio_mix_compile(&program)==JOLT_OK);
    size_t length; const uint8_t *code=jolt_program_data(program,&length);
    jolt_vm_t *vm=jolt_vm_create(); assert(vm);
    FILE *file=fopen(argv[1],"rb"); assert(file); char line[1024]; unsigned cases=0;
    while (fgets(line,sizeof(line),file)) {
        if (line[0]!='(') continue;
        float values[10];
        assert(sscanf(line,"(assert_audio_mix %f %f %f %f %f %f %f %f %f %f)",
            &values[0],&values[1],&values[2],&values[3],&values[4],&values[5],&values[6],&values[7],&values[8],&values[9])==10);
        float result[4]; assert(jolt_program_run(vm,program,values,8,result,4)==JOLT_OK);
        assert(fabsf(result[0]-values[8])<1.e-6f && fabsf(result[1]-values[9])<1.e-6f); ++cases;
    }
    fclose(file); assert(cases==6);
    float input[8]={1,-1,0.5f,-0.5f,0.25f,0.25f,-0.25f,-0.25f},out[8]={0},expected[8];
    const float samples[]={0,0,0.125f,-0.25f,0.125f,0.25f,-0.1875f,-0.375f};
    memcpy(expected,samples,sizeof(samples));
    assert(jolt_audio_accumulate(code,length,input,4,2,0.5f,0,4,4,0,out)==JOLT_OK);
    for (size_t i=0;i<8;++i) assert(fabsf(expected[i]-out[i])<1.e-6f);
    float unchanged[8]; memcpy(unchanged,out,sizeof(out)); input[6]=NAN;
    assert(jolt_audio_accumulate(code,length,input,4,2,0,0,4,0,0,out)==JOLT_ERR_NUMERIC);
    assert(!memcmp(out,unchanged,sizeof(out)));
    input[6]=0;
    assert(jolt_audio_accumulate(NULL,0,input,4,1,0,0,4,0,0,out)!=JOLT_OK);
    assert(jolt_audio_accumulate(code,length,NULL,4,1,0,0,4,0,0,out)==JOLT_ERR_ARGUMENT);
    assert(jolt_audio_accumulate(code,length,input,4,1,0,0,4,0,0,NULL)==JOLT_ERR_ARGUMENT);
    assert(jolt_audio_accumulate(code,length,input,4,1,2,0,4,0,0,out)==JOLT_ERR_ARGUMENT);
    jolt_vm_destroy(vm); jolt_program_destroy(program); return 0;
}
