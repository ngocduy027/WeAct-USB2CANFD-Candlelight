#!/usr/bin/env python3
"""Run the real flash bounds/write code against a fixed-address RAM model."""
import unittest
from test_regressions import ROOT, function, run_c


class FlashBounds(unittest.TestCase):
    def test_reserved_pages_and_write_failures(self):
        path = ROOT / 'Source/system.c'
        funcs = '\n'.join(function(path, sig) for sig in [
            'uint32_t system_get_flash_addr(', 'eFeedback system_write_flash('])
        run_c(r'''
// Host fortify object-size analysis cannot model the fixed-address flash mapping.
#undef _FORTIFY_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define __user_flash_start ((void *)0x0801C000)
#define __user_flash_end ((void *)0x08020000)
#define FLASH_PAGE_SIZE 2048U
#define FLASH_BASE 0x08000000U
#define MAX_FLASH_DATA_LEN 2046U
#define FBK_Success 2
#define FBK_ErrorFromHAL 44
#define FBK_AdapterMustBeClosed 52
#define FBK_ParamOutOfRange 61
#define HAL_OK 0
#define HAL_ERROR 1
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_BANK_1 1
#define FLASH_TYPEPROGRAM_DOUBLEWORD 1
#define FLASH_FLAG_ALL_ERRORS 1
#define __HAL_FLASH_CLEAR_FLAG(flags) ((void)(flags))
typedef int eFeedback;
typedef struct {unsigned TypeErase,Page,Banks,NbPages;} FLASH_EraseInitTypeDef;
static bool opened,unlocked,fail_unlock,fail_erase,fail_program,fail_lock;
static unsigned erases,programs;
static bool can_is_any_open(void){return opened;}
static int HAL_FLASH_Unlock(void){unlocked=!fail_unlock;return fail_unlock?HAL_ERROR:HAL_OK;}
static int HAL_FLASH_Lock(void){unlocked=false;return fail_lock?HAL_ERROR:HAL_OK;}
static int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *error){
 assert(unlocked && e->Banks==1 && e->Page>=56 && e->Page<64 && e->NbPages==1);
 erases++;if(fail_erase)return HAL_ERROR;
 memset((void *)(uintptr_t)(FLASH_BASE+e->Page*2048U),0xFF,2048);return HAL_OK;
}
static int HAL_FLASH_Program(unsigned type,uint32_t addr,uint64_t word){
 assert(unlocked && addr>=0x0801C000 && addr<=0x0801FFF8 && !(addr&7));programs++;
 if(fail_program)return HAL_ERROR;
 memcpy((void *)(uintptr_t)addr,&word,8);return HAL_OK;
}
''' + funcs + r'''
int main(void){
 void *mem=mmap((void *)FLASH_BASE,131072,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(mem==(void *)FLASH_BASE);memset(mem,0x5A,131072);memset(__user_flash_start,0xFF,16384);
 for(unsigned page=0;page<256;page++){
  uint32_t address=system_get_flash_addr(page);
  assert(address==(page<8?0x08020000U-(page+1)*2048U:0));
 }
 assert(!system_get_flash_addr(UINT32_MAX));
 // Exercise odd/aligned and maximum lengths; padding may not read old bytes.
 uint8_t storage[2052];uint8_t *input=storage+1;
 for(unsigned n=1;n<=2046;n++){
  for(unsigned i=0;i<n;i++)input[i]=(uint8_t)(i+n);
  assert(system_write_flash(n%8,input,n)==FBK_Success && !unlocked);
  uint8_t *page=(void *)(uintptr_t)system_get_flash_addr(n%8);
  uint16_t length;memcpy(&length,page,2);assert(length==n);
  for(unsigned i=0;i<n;i++)assert(page[2+i]==(uint8_t)(i+n));
  for(unsigned i=n+2;i<2048;i++)assert(page[i]==0xFF);
 }
 unsigned old_erases=erases;
 opened=true;assert(system_write_flash(0,input,1)==FBK_AdapterMustBeClosed && erases==old_erases);
 opened=false;assert(system_write_flash(8,input,1)==FBK_ParamOutOfRange);
 assert(system_write_flash(0,input,2047)==FBK_ParamOutOfRange);
 fail_unlock=true;assert(system_write_flash(0,input,1)==FBK_ErrorFromHAL && !unlocked);fail_unlock=false;
 fail_erase=true;assert(system_write_flash(0,input,1)==FBK_ErrorFromHAL && !unlocked);fail_erase=false;
 fail_program=true;assert(system_write_flash(0,input,1)==FBK_ErrorFromHAL && !unlocked);fail_program=false;
 fail_lock=true;assert(system_write_flash(0,input,1)==FBK_ErrorFromHAL && !unlocked);fail_lock=false;
 assert(system_write_flash(0,input,0)==FBK_Success && !unlocked);
 uint8_t *page=(void *)(uintptr_t)system_get_flash_addr(0);for(unsigned i=0;i<2048;i++)assert(page[i]==0xFF);
 for(unsigned i=0;i<112*1024;i++)assert(((uint8_t *)mem)[i]==0x5A); // No firmware-page mutation, even on injected failures.
 assert(programs && erases);assert(!munmap(mem,131072));return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
