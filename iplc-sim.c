/***********************************************************************/
/***********************************************************************
 Pipeline Cache Simulator Solution
 ***********************************************************************/
/***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define MAX_CACHE_SIZE 10240
#define CACHE_MISS_DELAY 10 // 10 cycle cache miss penalty
#define MAX_STAGES 5

// init the simulator
void iplc_sim_init(int index, int blocksize, int assoc);

// Cache simulator functions
void iplc_sim_LRU_replace_on_miss(int index, int tag);
void iplc_sim_LRU_update_on_hit(int index, int assoc);
int iplc_sim_trap_address(unsigned int address);

// Pipeline functions
unsigned int iplc_sim_parse_reg(char *reg_str);
void iplc_sim_parse_instruction(char *buffer);
void iplc_sim_push_pipeline_stage();
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg,
                                     int reg1, int reg2_or_constant);
void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_branch(int reg1, int reg2);
void iplc_sim_process_pipeline_jump();
void iplc_sim_process_pipeline_syscall();
void iplc_sim_process_pipeline_nop();

// Outout performance results
void iplc_sim_finalize();



typedef struct associativity
{
    int vb; /* valid bit */
    int tag;
} assoc_t;

typedef struct cache_line
{
    assoc_t *assoc;
    int     *replacement;
} cache_line_t;

cache_line_t *cache=NULL;
int cache_index=0;
int cache_blocksize=0;
int cache_blockoffsetbits = 0;
int cache_assoc=0;
long cache_miss=0;
long cache_access=0;
long cache_hit=0;

char instruction[16];
char reg1[16];
char reg2[16];
char offsetwithreg[16];
unsigned int data_address=0;
unsigned int instruction_address=0;
unsigned int pipeline_cycles=0;   // how many cycles did you pipeline consume
unsigned int instruction_count=0; // home many real instructions ran thru the pipeline
unsigned int branch_predict_taken=0;
unsigned int branch_count=0;
 unsigned int correct_branch_predictions=0;

// //Added these vars
// unsigned int temp_instruction_address = 0;
// unsigned int missed_cont_cycles=0;

unsigned int debug=0;
unsigned int dump_pipeline=1;


enum instruction_type {NOP, RTYPE, LW, SW, BRANCH, JUMP, JAL, SYSCALL};
//testing commit
typedef struct rtype
{
    char instruction[16];
    int reg1;
    int reg2_or_constant;
    int dest_reg;
    
} rtype_t;

typedef struct load_word
{
    unsigned int data_address;
    int dest_reg;
    int base_reg;
    
} lw_t;

typedef struct store_word
{
    unsigned int data_address;
    int src_reg;
    int base_reg;
} sw_t;

typedef struct branch
{
    int reg1;
    int reg2;
    
} branch_t;


typedef struct jump
{
    char instruction[16];
    
} jump_t;

typedef struct pipeline
{
    enum instruction_type itype;
    unsigned int instruction_address;
    union
    {
        rtype_t   rtype;
        lw_t      lw;
        sw_t      sw;
        branch_t  branch;
        jump_t    jump;
    }
    stage;
    
} pipeline_t;

enum pipeline_stages {FETCH, DECODE, ALU, MEM, WRITEBACK};

pipeline_t pipeline[MAX_STAGES];

/************************************************************************************************/
/* Cache Functions ******************************************************************************/
/************************************************************************************************/
/*
 * Correctly configure the cache.
 */
void iplc_sim_init(int index, int blocksize, int assoc)
{
    int i=0, j=0;
    unsigned long cache_size = 0;
    cache_index = index;
    cache_blocksize = blocksize;
    cache_assoc = assoc;
    
    
    cache_blockoffsetbits =
    (int) rint((log( (double) (blocksize * 4) )/ log(2)));
    /* Note: rint function rounds the result up prior to casting */
    
    cache_size = assoc * ( 1 << index ) * ((32 * blocksize) + 33 - index - cache_blockoffsetbits);
    
    printf("Cache Configuration \n");
    printf("   Index: %d bits or %d lines \n", cache_index, (1<<cache_index) );
    printf("   BlockSize: %d \n", cache_blocksize );
    printf("   Associativity: %d \n", cache_assoc );
    printf("   BlockOffSetBits: %d \n", cache_blockoffsetbits );
    printf("   CacheSize: %lu \n", cache_size );
    
    if (cache_size > MAX_CACHE_SIZE ) {
        printf("Cache too big. Great than MAX SIZE of %d .... \n", MAX_CACHE_SIZE);
        exit(-1);
    }
    
    //array of cache_line_t's
    cache = (cache_line_t *) malloc((sizeof(cache_line_t) * 1<<index));
    
    for (i = 0; i < (1<<index); i++) {
        cache[i].assoc = (assoc_t *)malloc((sizeof(assoc_t) * assoc));
        cache[i].replacement = (int *)malloc((sizeof(int) * assoc));
        
        for (j = 0; j < assoc; j++) {
            cache[i].assoc[j].vb = 0;
            cache[i].assoc[j].tag = 0;
            cache[i].replacement[j] = j;
        }
    }
    
    // init the pipeline -- set all data to zero and instructions to NOP
    for (i = 0; i < MAX_STAGES; i++) {
        // itype is set to O which is NOP type instruction
        bzero(&(pipeline[i]), sizeof(pipeline_t));
    }
}

/*
 * iplc_sim_trap_address() determined this is not in our cache.  Put it there
 * and make sure that is now our Most Recently Used (MRU) entry.
 */
void iplc_sim_LRU_replace_on_miss(int index, int tag)
{
    int i=0, j=0;
    
    /* Note: item 0 is the least recently used cache slot -- so replace it */
    
    //find where in the cache we have an invalid bit, and replace that slot with our new tag and valid bit
    for(i = 0; i < cache_assoc; i++) {
        if(cache[index].assoc[i].vb != 1) {
            break;
        }
    }

    /* percolate everything up */
    for(j = 1; j < i; j++) {        
        int currentTag = cache[index].assoc[j].tag;
        cache[index].assoc[j-1].tag = currentTag;
    }

    if(i == cache_assoc) {
        cache[index].assoc[i-1].tag = tag;
        cache[index].assoc[i-1].vb = 1;
    }
    else {
        cache[index].assoc[i].tag = tag;
        cache[index].assoc[i].vb = 1;
    }

    //increment our cache miss count
    cache_miss++;
    
}


/*
 * iplc_sim_trap_address() determined the entry is in our cache.  Update its
 * information in the cache.
 */
void iplc_sim_LRU_update_on_hit(int index, int assoc)
{
    int i=0, j=0;

    for (j = 0; j < cache_assoc; j++)
        if (cache[index].replacement[j] == assoc)
            break;
    
    /* percolate everything up */
    for (i = j+1; i < cache_assoc; i++) {
        cache[index].replacement[i-1] = cache[index].replacement[i];
    }
    
    cache[index].replacement[cache_assoc-1] = assoc;
    cache_hit++;
}

/*
 * Check if the address is in our cache.  Update our counter statistics 
 * for cache_access, cache_hit, etc.  If our configuration supports
 * associativity we may need to check through multiple entries for our
 * desired index.  In that case we will also need to call the LRU functions.
 */
int iplc_sim_trap_address(unsigned int address)
{

    int i=0, index=0;
    int tag=0;

    //Calculate our index and tag using bit masking based off of user inputted parameters
    index = (address >> cache_blockoffsetbits) % (1 << cache_index);
    tag = address >> (cache_blockoffsetbits + cache_index);

    //print out current index, address and tag on each instruction..
    printf("Address %x: Tag= %x, Index= %x\n", address, tag, index);

    //Using on our index and tag values we can update our cache based on whether we have a hit or miss
    for(i = 0; i < cache_assoc; i++) {

        if(cache[index].assoc[i].tag == tag && cache[index].assoc[i].vb == 1) {
            //we know that we've found a hit.. set hit to true and update our cache
            iplc_sim_LRU_update_on_hit(index, i);
            return 1; 

        }
    }   
    /* expects you to return 1 for hit, 0 for miss */
    iplc_sim_LRU_replace_on_miss(index, tag);
    return 0;
}
/*
 * Just output our summary statistics.
 */
void iplc_sim_finalize()
{
    /* Finish processing all instructions 
    in the Pipeline */
    while (pipeline[FETCH].itype != NOP  ||
           pipeline[DECODE].itype != NOP ||
           pipeline[ALU].itype != NOP    ||
           pipeline[MEM].itype != NOP    ||
           pipeline[WRITEBACK].itype != NOP) {
        iplc_sim_push_pipeline_stage();
    }
    
    printf(" Cache Performance \n");
    printf("\t Number of Cache Accesses is %ld \n", cache_access);
    printf("\t Number of Cache Misses is %ld \n", cache_miss);
    printf("\t Number of Cache Hits is %ld \n", cache_hit);
    printf("\t Cache Miss Rate is %f \n\n", (double)cache_miss / (double)cache_access);
    printf("Pipeline Performance \n");
    printf("\t Total Cycles is %u \n", pipeline_cycles);
    printf("\t Total Instructions is %u \n", instruction_count);
    printf("\t Total Branch Instructions is %u \n", branch_count);
    printf("\t Total Correct Branch Predictions is %u \n", correct_branch_predictions);
    printf("\t CPI is %f \n\n", (double)pipeline_cycles / (double)instruction_count);
}

/************************************************************************************************/
/* Pipeline Functions ***************************************************************************/
/************************************************************************************************/

/*
 * Dump the current contents of our pipeline.
 */
void iplc_sim_dump_pipeline()
{
    int i;
    
    for (i = 0; i < MAX_STAGES; i++) {
        switch(i) {
            case FETCH:


                printf("(cyc: %u) FETCH:\t %d: 0x%x \t", pipeline_cycles, pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case DECODE:
                printf("DECODE:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case ALU:
                printf("ALU:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case MEM:
                printf("MEM:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case WRITEBACK:
                printf("WB:\t %d: 0x%x \n", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            default:
                printf("DUMP: Bad stage!\n" );
                exit(-1);
        }
    }
}
/*
 * Check if various stages of our pipeline require stalls, forwarding, etc.
 * Then push the contents of our various pipeline stages through the pipeline.
 */
void iplc_sim_push_pipeline_stage()
{
    
    /* 1. Count WRITEBACK stage is "retired" -- This I'm giving you */
    if (pipeline[WRITEBACK].instruction_address) {
        instruction_count++;
        if (debug)
            printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n",
                   pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles);
    }
    
    /* 2. Check for BRANCH and correct/incorrect Branch Prediction */
    int branch_taken = 0;
    if (pipeline[DECODE].itype == BRANCH){
        
        if (pipeline[FETCH].instruction_address != (pipeline[DECODE].instruction_address + 4)){
                branch_taken = 1;
         }
        
        // This branch instruction is taken
        if (branch_taken){
            
            //if Branch Prediction on the Mode Taken, then the branch prediction was correct
            if(branch_predict_taken){
                correct_branch_predictions++;
            }
            
            // If the prediciton was wrong then there is a penalty
            else{
                pipeline_cycles++;
                
                // Forward DECODE to WRITEBACK 
                pipeline[WRITEBACK].itype = pipeline[MEM].itype;
                pipeline[WRITEBACK].instruction_address = pipeline[MEM].instruction_address;

                pipeline[MEM].itype = pipeline[ALU].itype;
                pipeline[MEM].instruction_address = pipeline[ALU].instruction_address;

                pipeline[ALU].itype = pipeline[DECODE].itype;
                pipeline[ALU].instruction_address = pipeline[DECODE].instruction_address;

                // Place NOP in DECODE
                pipeline[DECODE].itype = NOP;
                pipeline[DECODE].instruction_address = 0x0;
                
                // If LW
                if (pipeline[WRITEBACK].itype == LW){
                    pipeline[WRITEBACK].stage.lw.data_address = pipeline[MEM].stage.lw.data_address;
                }
                if (pipeline[MEM].itype == LW){
                    pipeline[MEM].stage.lw.data_address = pipeline[ALU].stage.lw.data_address;
                }
                if (pipeline[ALU].itype == LW){
                    pipeline[ALU].stage.lw.data_address = pipeline[DECODE].stage.lw.data_address;
                }

                // If SW
                if (pipeline[WRITEBACK].itype == SW){
                    pipeline[WRITEBACK].stage.sw.data_address = pipeline[MEM].stage.sw.data_address;
                }
                
                if (pipeline[MEM].itype == SW){
                    pipeline[MEM].stage.sw.data_address = pipeline[ALU].stage.sw.data_address;
                }

                if (pipeline[ALU].itype == SW){
                    pipeline[ALU].stage.sw.data_address = pipeline[DECODE].stage.sw.data_address;
                }
                
                //Not NOP in WB, then add 1 to IC
                if( pipeline[WRITEBACK].instruction_address ){
                    instruction_count++;    
                }
            }           
        }
        // This branch instruction is not taken
        else if (!branch_taken){
            
            //if Branch Prediction on the Mode Not Taken, then the branch prediction was correct
            if(!branch_predict_taken){
                correct_branch_predictions++;
            }
            
            // If the prediciton was wrong then there is a penalty
            else{
                pipeline_cycles++;
                
                // Forward DECODE to WRITEBACK 
                pipeline[WRITEBACK].itype = pipeline[MEM].itype;
                pipeline[WRITEBACK].instruction_address = pipeline[MEM].instruction_address;

                pipeline[MEM].itype = pipeline[ALU].itype;
                pipeline[MEM].instruction_address = pipeline[ALU].instruction_address;

                pipeline[ALU].itype = pipeline[DECODE].itype;
                pipeline[ALU].instruction_address = pipeline[DECODE].instruction_address;

                // Place NOP in DECODE
                pipeline[DECODE].itype = NOP;
                pipeline[DECODE].instruction_address = 0x0;

                // If LW
                if (pipeline[WRITEBACK].itype == LW){
                    pipeline[WRITEBACK].stage.lw.data_address = pipeline[MEM].stage.lw.data_address;
                }

           
                if (pipeline[MEM].itype == LW){
                    pipeline[MEM].stage.lw.data_address = pipeline[ALU].stage.lw.data_address;
                }
                    
          
                if (pipeline[ALU].itype == LW){
                    pipeline[ALU].stage.lw.data_address = pipeline[DECODE].stage.lw.data_address;
                }
                
                // If SW
                if (pipeline[WRITEBACK].itype == SW){
                    pipeline[WRITEBACK].stage.sw.data_address = pipeline[MEM].stage.sw.data_address;
                }
                
                
                if (pipeline[MEM].itype == SW){
                    pipeline[MEM].stage.sw.data_address = pipeline[ALU].stage.sw.data_address;
                }

               
                if (pipeline[ALU].itype == SW){
                    pipeline[ALU].stage.sw.data_address = pipeline[DECODE].stage.sw.data_address;
                }
                //Not NOP in WB, then add 1 to IC

                if( pipeline[WRITEBACK].instruction_address ){
                    instruction_count++;    
                }
            }  
        }
    }

    /* 3. Check for LW delays due to use in ALU stage and if data hit/miss
     *    add delay cycles if needed.
     */
   if (pipeline[MEM].itype == LW) {
        int instructionAddress = pipeline[MEM].stage.lw.data_address;
        if(!iplc_sim_trap_address(instructionAddress)) {
            pipeline_cycles += 9;
            printf("DATA MISS Address 0x%x\n", instructionAddress);
        }
        else {
            printf("DATA HIT Address 0x%x\n", instructionAddress);
        }
    }
    
    /* 4. Check for SW mem acess and data miss .. add delay cycles if needed */
    if (pipeline[MEM].itype == SW) {
        int instructionAddress = pipeline[MEM].stage.sw.data_address;
        if(!iplc_sim_trap_address(instructionAddress)) {
            pipeline_cycles += 9;
            printf("DATA MISS Address 0x%x", instructionAddress);
        }
        else {
            printf("DATA HIT Address 0x%x", instructionAddress);
        }
    }
    
    /* 5. Increment pipe_cycles 1 cycle for normal processing */
    pipeline_cycles++;
    /* 6. push stages thru MEM Forwarded to WB, ALU Forwarded to MEM, DECODE Forwarded to ALU, FETCH Forwarded to ALU */
    // MEM Forwarded to WB 
    pipeline[WRITEBACK].itype = pipeline[MEM].itype;
    pipeline[WRITEBACK].instruction_address = pipeline[MEM].instruction_address;
    
    //LW
    if (pipeline[WRITEBACK].itype == LW){
        pipeline[WRITEBACK].stage.lw.data_address = pipeline[MEM].stage.lw.data_address;
    }
    // SW
    if (pipeline[WRITEBACK].itype == SW){
        pipeline[WRITEBACK].stage.sw.data_address = pipeline[MEM].stage.sw.data_address;
    }
    
    //ALU Forwarded to MEM 
    pipeline[MEM].itype = pipeline[ALU].itype;
    pipeline[MEM].instruction_address = pipeline[ALU].instruction_address;
    
    // LW
    if (pipeline[MEM].itype == LW){
        pipeline[MEM].stage.lw.data_address = pipeline[ALU].stage.lw.data_address;
    }
    // SW
    if (pipeline[MEM].itype == SW){
        pipeline[MEM].stage.sw.data_address = pipeline[ALU].stage.sw.data_address;
    }
    
    //DECODE Forwarded to ALU
    pipeline[ALU].itype = pipeline[DECODE].itype;
    pipeline[ALU].instruction_address = pipeline[DECODE].instruction_address;
    
    // LW
    if (pipeline[ALU].itype == LW){
        pipeline[ALU].stage.lw.data_address = pipeline[DECODE].stage.lw.data_address;
    }
    // SW
    if (pipeline[ALU].itype == SW){
        pipeline[ALU].stage.sw.data_address = pipeline[DECODE].stage.sw.data_address;
    }
    
    //FETCH Forwarded to DECODE
    pipeline[DECODE].itype = pipeline[FETCH].itype;
    pipeline[DECODE].instruction_address = pipeline[FETCH].instruction_address;
    
    // LW
    if (pipeline[DECODE].itype == LW){
        pipeline[DECODE].stage.lw.data_address = pipeline[FETCH].stage.lw.data_address;
    }
    // SW
    if (pipeline[DECODE].itype == SW){
        pipeline[DECODE].stage.sw.data_address = pipeline[FETCH].stage.sw.data_address;
    }
    
    //handeling our register forwarding..
    if(pipeline[DECODE].stage.rtype.reg1 == pipeline[ALU].stage.rtype.dest_reg){
        pipeline[ALU].stage.rtype.dest_reg = pipeline[DECODE].stage.rtype.reg1;
    }
    if(pipeline[DECODE].stage.rtype.reg2_or_constant == pipeline[ALU].stage.rtype.dest_reg){
        pipeline[ALU].stage.rtype.dest_reg = pipeline[DECODE].stage.rtype.reg2_or_constant;
    }
    if(pipeline[DECODE].stage.rtype.reg1 == pipeline[MEM].stage.rtype.dest_reg){
        pipeline[MEM].stage.rtype.dest_reg = pipeline[DECODE].stage.rtype.reg1;
    }
    if(pipeline[DECODE].stage.rtype.reg2_or_constant == pipeline[MEM].stage.rtype.dest_reg){
        pipeline[MEM].stage.rtype.dest_reg = pipeline[DECODE].stage.rtype.reg2_or_constant;
    }
    
    // 7. This is a give'me -- Reset the FETCH stage to NOP via bezero */
    bzero(&(pipeline[FETCH]), sizeof(pipeline_t));

}

/*
 * This function is fully implemented.  You should use this as a reference
 * for implementing the remaining instruction types.
 */
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg, int reg1, int reg2_or_constant) {
    /* This is an example of what you need to do for the rest */
    iplc_sim_push_pipeline_stage();
    
    pipeline[FETCH].itype = RTYPE;
    pipeline[FETCH].instruction_address = instruction_address;
    
    strcpy(pipeline[FETCH].stage.rtype.instruction, instruction);
    pipeline[FETCH].stage.rtype.reg1 = reg1;
    pipeline[FETCH].stage.rtype.reg2_or_constant = reg2_or_constant;
    pipeline[FETCH].stage.rtype.dest_reg = dest_reg;
}

void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address) {
    

    iplc_sim_push_pipeline_stage();
   
    pipeline[FETCH].itype = LW;
    pipeline[FETCH].instruction_address = instruction_address;

    pipeline[FETCH].stage.lw.base_reg = base_reg;
    pipeline[FETCH].stage.lw.dest_reg = dest_reg;
    pipeline[FETCH].stage.lw.data_address = data_address;
    //incremenet cache access count because we accessed the cache to grab the desired data
    cache_access++;


}

void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address) {
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = SW;
    pipeline[FETCH].instruction_address = instruction_address;
    pipeline[FETCH].stage.sw.base_reg = src_reg;
    pipeline[FETCH].stage.sw.base_reg = base_reg;
    pipeline[FETCH].stage.sw.data_address = data_address;
    
    //incremenet cache access count because we accessed the cache to grab the desired data
    cache_access++;

}

void iplc_sim_process_pipeline_branch(int reg1, int reg2) {
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = BRANCH;
    pipeline[FETCH].instruction_address = instruction_address;
    
    pipeline[FETCH].stage.branch.reg1 = reg1;
    pipeline[FETCH].stage.branch.reg2 = reg2;
}

void iplc_sim_process_pipeline_jump(char *instruction) {
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = JUMP;
    pipeline[FETCH].instruction_address = instruction_address;
    strcpy( pipeline[FETCH].stage.jump.instruction, instruction );
}

void iplc_sim_process_pipeline_syscall() {
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = SYSCALL;
    pipeline[FETCH].instruction_address = instruction_address;


}

void iplc_sim_process_pipeline_nop() {
    iplc_sim_push_pipeline_stage();
    pipeline[FETCH].itype = NOP;
    pipeline[FETCH].instruction_address = instruction_address;

}
/************************************************************************************************/
/* parse Function *******************************************************************************/
/************************************************************************************************/

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
unsigned int iplc_sim_parse_reg(char *reg_str)
{
    int i;
    // turn comma into \n
    if (reg_str[strlen(reg_str)-1] == ',')
        reg_str[strlen(reg_str)-1] = '\n';
    
    if (reg_str[0] != '$')
        return atoi(reg_str);
    else {
        // copy down over $ character than return atoi
        for (i = 0; i < strlen(reg_str); i++)
            reg_str[i] = reg_str[i+1];
        
        return atoi(reg_str);
    }
}

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
void iplc_sim_parse_instruction(char *buffer)
{
    int instruction_hit = 0;
    int i=0, j=0;
    int src_reg=0;
    int src_reg2=0;
    int dest_reg=0;
    char str_src_reg[16];
    char str_src_reg2[16];
    char str_dest_reg[16];
    char str_constant[16];
    
    if (sscanf(buffer, "%x %s", &instruction_address, instruction ) != 2) {
        printf("Malformed instruction \n");
        exit(-1);
    }


    
    instruction_hit = iplc_sim_trap_address( instruction_address );
    cache_access++;
    
    // if a MISS, then push current instruction thru pipeline
    if (!instruction_hit) {
        // need to subtract 1, since the stage is pushed once more for actual instruction processing
        // also need to allow for a branch miss prediction during the fetch cache miss time -- by
        // counting cycles this allows for these cycles to overlap and not doubly count.
        
        printf("INST MISS:\t Address 0x%x \n", instruction_address);
        

        for (i = pipeline_cycles, j = pipeline_cycles; i < j + CACHE_MISS_DELAY - 1; i++)
            iplc_sim_push_pipeline_stage();
    }
    else
        printf("INST HIT:\t Address 0x%x \n", instruction_address);
    
    // Parse the Instruction
    
    if (strncmp( instruction, "add", 3 ) == 0 ||
        strncmp( instruction, "sll", 3 ) == 0 ||
        strncmp( instruction, "ori", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_src_reg,
                   str_src_reg2 ) != 5) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address);
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = iplc_sim_parse_reg(str_src_reg);
        src_reg2 = iplc_sim_parse_reg(str_src_reg2);
        
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lui", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_constant ) != 4 ) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address );
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = -1;
        src_reg2 = -1;
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lw", 2 ) == 0 ||
             strncmp( instruction, "sw", 2 ) == 0  ) {
        if ( sscanf( buffer, "%x %s %s %s %x",
                    &instruction_address,
                    instruction,
                    reg1,
                    offsetwithreg,
                    &data_address ) != 5) {
            printf("Bad instruction: %s at address %x \n", instruction, instruction_address);
            exit(-1);
        }
        
        if (strncmp(instruction, "lw", 2 ) == 0) {
            
            dest_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_lw(dest_reg, -1, data_address);
        }
        if (strncmp( instruction, "sw", 2 ) == 0) {
            src_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_sw( src_reg, -1, data_address);
        }
    }
    else if (strncmp( instruction, "beq", 3 ) == 0) {
        branch_count++;
        // don't need to worry about getting regs -- just insert -1 values
        iplc_sim_process_pipeline_branch(-1, -1);
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        iplc_sim_process_pipeline_jump( instruction );
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        /*
         * Note: no need to worry about forwarding on the jump register
         * we'll let that one go.
         */
        iplc_sim_process_pipeline_jump(instruction);
    }
    else if ( strncmp( instruction, "syscall", 7 ) == 0) {
        iplc_sim_process_pipeline_syscall( );
    }
    else if ( strncmp( instruction, "nop", 3 ) == 0) {
        iplc_sim_process_pipeline_nop( );
    }
    else {
        printf("Do not know how to process instruction: %s at address %x \n",
               instruction, instruction_address );
        exit(-1);
    }
}

/************************************************************************************************/
/* MAIN Function ********************************************************************************/
/************************************************************************************************/

int main()
{
    char trace_file_name[1024];
    FILE *trace_file = NULL;
    char buffer[80];
    int index = 10;
    int blocksize = 1;
    int assoc = 1;
    
    printf("Please enter the tracefile: ");
    scanf("%s", trace_file_name);
    
    trace_file = fopen(trace_file_name, "r");
    
    if ( trace_file == NULL ) {
        printf("fopen failed for %s file\n", trace_file_name);
        exit(-1);
    }
    
    printf("Enter Cache Size (index), Blocksize and Level of Assoc \n");
    scanf( "%d %d %d", &index, &blocksize, &assoc );
    
    printf("Enter Branch Prediction: 0 (NOT taken), 1 (TAKEN): ");
    scanf("%d", &branch_predict_taken );
    
    iplc_sim_init(index, blocksize, assoc);
    
    while (fgets(buffer, 80, trace_file) != NULL) {
        iplc_sim_parse_instruction(buffer);
        if (dump_pipeline)
            iplc_sim_dump_pipeline();
    }
    
    iplc_sim_finalize();
    return 0;
}
