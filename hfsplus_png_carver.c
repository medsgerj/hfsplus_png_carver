#include <stdio.h>
#include <hfs/hfs_format.h>
#include <libkern/OSByteOrder.h>
#include <libc.h>
 
#define SECTOR_SIZE 512
#define UNALLOC_CHUNK_SIZE_IN_BLOCKS 4000
 
unsigned char png_header[8]   =
{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
uint16_t      png_header_size = 8;
unsigned char png_footer[8]   = 
{0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
uint16_t      png_footer_size = 8; 
 
/* DETERMINE IF THAT ALLOCATION BLOCK IS ALLOCATED */
int is_block_allocated(uint64_t block, unsigned char *alloc_file_buf){
 
  uint8_t b;
 
  b = alloc_file_buf[block / 8];
 
  return ( b & (128 >> ((block % 8))) ) != 0;
}
 
int allocation_file_bmp_init(HFSPlusVolumeHeader *vh)
{
  /* RESET ALL THE VOLUME HEADER VARIABLES (CORRECT BYTE ORDER) */    
  vh->blockSize = OSSwapBigToHostInt32(vh->blockSize);
  vh->freeBlocks = OSSwapBigToHostInt32(vh->freeBlocks);
  vh->totalBlocks = OSSwapBigToHostInt32(vh->totalBlocks);                                
 
  /* CONVERT THE BYTE ORDER OF NECESSARY ALLOCATION FILE STRUCT VARIABLES */
  (&(vh->allocationFile))->logicalSize = 
  OSSwapBigToHostInt64( (&(vh->allocationFile))->logicalSize );
  (&(vh->allocationFile))->clumpSize = 
  OSSwapBigToHostInt32( (&(vh->allocationFile))->clumpSize );
  (&(vh->allocationFile))->totalBlocks = 
  OSSwapBigToHostInt32( (&(vh->allocationFile))->totalBlocks );
 
  /* CONVERT THE BYTE ORDER OF NECESSARY ALLOCATION FILE STRUCT VARIABLES */
  uint64_t i;
  for(i = 0; i < kHFSPlusExtentDensity; i++){
    (&(vh->allocationFile))->extents[i].startBlock = 
    OSSwapBigToHostInt32( (&(vh->allocationFile))->extents[i].startBlock);
    (&(vh->allocationFile))->extents[i].blockCount = 
    OSSwapBigToHostInt32( (&(vh->allocationFile))->extents[i].blockCount);
  }
 
  return 0;
}
 
int read_alloc_bitmap_into_mem(int fd,
                               HFSPlusVolumeHeader *vh, 
                               unsigned char *alloc_file_buf)
{
  uint64_t byte_count;
  uint64_t offset;
  uint64_t extent_length;
  unsigned char *buf;
 
  byte_count = 0;
 
  /* FOR EACH EXTENT OF THE ALLOCATION BITMAP FILE */
  uint64_t i;
  for(i = 0; i < kHFSPlusExtentDensity; i++){
 
    /* CALCULATE THE BYTE OFFSET AND LENGTH OF THAT EXTENT OF THE 
     * ALLOCATION FILE */
    offset = (&(vh->allocationFile))->extents[i].startBlock 
                                                   * vh->blockSize;
    extent_length = (&(vh->allocationFile))->extents[i].blockCount
                                                   * vh->blockSize;
 
    /* ALLOCATE MEMORY FOR THAT ALLOCATION FILE EXTENT */
    buf = (unsigned char *)malloc( extent_length * sizeof(unsigned char) );
    if (buf == NULL){
      printf("Error allocating memory for allocation file extent.\n");
      return -1;
    }    
 
    /* READ THAT ALLOCATION FILE EXTENT INTO A BUFFER */
    if (pread(fd, buf, extent_length * sizeof(unsigned char), offset) != 
        extent_length * sizeof(unsigned char) )
    {
      printf("Error reading allocation file extent.\n");
      return -1;
    }
 
    /* SEQUENTIALLY COPY ALLOCATION FILE EXTENT 
     * INTO THE ALLOCATION FILE BUFFER */
    uint64_t x;
    for (x = 0; x < extent_length * sizeof(unsigned char); x++){
      alloc_file_buf[byte_count] = buf[x];
      byte_count++;  
    }
 
    free(buf);
 
  } 
 
  return 0;
}
 
 uint64_t blocks_loaded = 0;
 
 
uint64_t get_unalloc_chunk(unsigned char *c, int fd, HFSPlusVolumeHeader *vh, 
                           unsigned char *alloc_file_buf, 
                           uint64_t offset, uint64_t chunk_size)
{
 
  uint64_t unalloc_block_num_at_offset = offset / vh->blockSize;
  uint64_t chunk_size_in_blocks = chunk_size / vh->blockSize;
  uint64_t arr_elem_count = 0;
  unsigned char *buf;
 
  buf = (unsigned char *)malloc(vh->blockSize * sizeof(unsigned char));
  if (buf == NULL){
    return -1;
  }
 
  uint64_t unalloc_block_count = 0;
  uint64_t j;
  for (j = 0; j < vh->totalBlocks; j++){
 
    /* SEE IF THIS BLOCK IS UNALLOCATED */
    int alloc_status = is_block_allocated(j, alloc_file_buf);
 
    if (alloc_status == 0){      
 
      if ((unalloc_block_count >= unalloc_block_num_at_offset) && 
          (unalloc_block_count < unalloc_block_num_at_offset 
                                    + chunk_size_in_blocks)){
 
        if (pread(fd, buf, vh->blockSize, j * vh->blockSize) 
                                           != vh->blockSize){
          printf("Error reading allocation block into memory.\n");
          free(buf);
          return -1;
        }
 
        memcpy((&c[arr_elem_count * vh->blockSize]), buf, vh->blockSize);
        arr_elem_count++;
      }
 
      unalloc_block_count++;
    }
  }  
 
  free(buf);
 
  return arr_elem_count * vh->blockSize;
 
} 
 
/* READS AN UNALLOCATED BYTE AT A GIVEN UNALLOCATED BYTE OFFSET */ 
int unalloc_byte(int fd, HFSPlusVolumeHeader *vh, 
                 unsigned char *alloc_file_buf, uint64_t offset){
 
  static uint8_t first_time;
  static unsigned char *c0;
  static unsigned char *c1;
  static unsigned char *c2;
 
  static uint64_t start_offset_c0;
  static uint64_t start_offset_c1;
  static uint64_t start_offset_c2;
 
  static uint64_t ct0;
  static uint64_t ct1;
  static uint64_t ct2;
 
  static uint64_t counter;
 
  uint64_t unalloc_block_num_at_offset = offset / vh->blockSize;
  uint64_t unalloc_block_num_at_offset_offset = 
                         unalloc_block_num_at_offset * vh->blockSize;
  uint64_t chunk_size = UNALLOC_CHUNK_SIZE_IN_BLOCKS * vh->blockSize;
 
  uint8_t in_c0_flag = 0;
  uint8_t in_c1_flag = 0;
  uint8_t in_c2_flag = 0;
 
  counter++;
 
  /* IF THIS IS THE FIRST TIME THE FUNCTION HAS BEEN CALLED */
  if (c0 == NULL || c1 == NULL || c2 == NULL){
    c0 = (unsigned char *)malloc(chunk_size * sizeof(unsigned char));
    if (c0 == NULL){
      return -1;
    }
 
    c1 = (unsigned char *)malloc(chunk_size * sizeof(unsigned char));
    if (c1 == NULL){
      return -1;
    }
 
    c2 = (unsigned char *)malloc(chunk_size * sizeof(unsigned char));
    if (c2 == NULL){
      return -1;
    }
 
    first_time = 1;
  }
 
  /* IF OFFSET IS LESS THAN START OFFSET OF FIRST BUFFER CHUNK OR GREATER
   * THAN THE OFFSET OF THE END OF THE LAST BUFFER CHUNK OR IF IT IS THE 
   * FIRST TIME THE FUNCTION HAS BEEN CALLED */
  if ( offset < start_offset_c0  || offset > start_offset_c2 + ct2 ||
       first_time == 1)
  {
    ct0 = get_unalloc_chunk(c0, fd, vh, alloc_file_buf, 
          unalloc_block_num_at_offset_offset, chunk_size);
    ct1 = get_unalloc_chunk(c1, fd, vh, alloc_file_buf, 
          unalloc_block_num_at_offset_offset+ct0, chunk_size);
    ct2 = get_unalloc_chunk(c2, fd, vh, alloc_file_buf, 
          unalloc_block_num_at_offset_offset+ct0+ct1, chunk_size);
 
    counter = 0;
 
    if (ct0 == -1 || ct1 == -1 || ct2 == -1){
      return -1;
    }
 
    start_offset_c0 = unalloc_block_num_at_offset_offset;
    start_offset_c1 = start_offset_c0 + ct0;
    start_offset_c2 = start_offset_c1 + ct1;
 
  }
 
  /* IF OFFSET IS WITHIN THE FIRST UNALLOCATED CHUNK IN MEM */
  if ((offset >= start_offset_c0) && (offset < start_offset_c0 + ct0)){
    in_c0_flag = 1;
  }
 
  /* IF OFFSET IS WITHIN THE SECOND UNALLOCATED CHUNK IN MEM */
  if ((offset >= start_offset_c1) && (offset < start_offset_c1 + ct1)){
    in_c1_flag = 1;
  }
 
  /* IF OFFSET IS WITHIN THE THIRD UNALLOCATED CHUNK IN MEM */
  if ((offset >= start_offset_c2) && (offset < start_offset_c2 + ct2)){
    in_c2_flag = 1;
  }
 
  if (in_c0_flag == 1){
    first_time = 0;
    return c0[offset % chunk_size];
  }
 
  if (in_c1_flag == 1){
    first_time = 0;
    return c1[offset % chunk_size]; 
  }
 
  /* IF IN THE THIRD UNALLOCATED CHUNK IN MEM, GET RID OF THE FIRST 
   * CHUNK IN MEM, SET THE SECOND CHUNK TO THE CURRENT THIRD CHUNK'S
   * SPACE, AND THEN LOAD A NEW THIRD CHUNK AFTER THE SECOND CHUNK */
  if (in_c2_flag == 1){
    c0 = c1;
    start_offset_c0 = start_offset_c1;
    c1 = c2;
    start_offset_c1 = start_offset_c2;
    ct2 = get_unalloc_chunk(c2, fd, vh, alloc_file_buf, 
                            start_offset_c1 + ct1, chunk_size);
 
    if (ct2 == -1){
      return -1;
    }
 
    start_offset_c2 = start_offset_c1 + ct1;   
 
    first_time = 0;
    return c1[offset % chunk_size];
  }
 
  first_time = 0;
 
} 
 
int main(int argc, char **argv)
{
  int                 fd;
  HFSPlusVolumeHeader *vh;
  unsigned char       buffer[SECTOR_SIZE];
  unsigned char       *alloc_file_buf;
  uint64_t            num_unallocated_blocks;
  uint64_t            num_allocated_blocks;
  unsigned char       *alloc_block_buf;
 
  uint8_t             file_found_flag;
  uint8_t             file_being_carved_flag;
  uint64_t            carved_count;
  FILE                *cfp;
  char                carved_file_name[4096];
 
  num_unallocated_blocks = 0;
  num_allocated_blocks = 0;
  carved_count = 0;
 
  /* OPEN THE DRIVE */
  fd = open(argv[1], O_RDONLY);
  if (fd == -1)
  {
    printf("Drive could not be opened.\n");
    exit(-1);
  }
 
  /* READ THE SECTOR THAT CONTAINS THE VOLUME HEADER */
  if (pread(fd, buffer, sizeof(buffer), 2 * SECTOR_SIZE) != sizeof(buffer))
  {
    printf("Error reading the Volume Header");
    return -1;
  }
 
  /* GET POINTER TO VOLUME HEADER */
  vh = (HFSPlusVolumeHeader *)buffer;
 
  /* INITIALIZE THE ALLOCATION FILE STRUCTS */
  if (allocation_file_bmp_init(vh) == -1)
  {
    printf("Error initializing the allocation file structures.\n");
    exit(-1);
  }
 
  printf("LOGICAL SIZE %llu\n", (unsigned long long) 
         (&(vh->allocationFile))->logicalSize );
  printf("CLUMP SIZE %lu\n",    (unsigned long) 
         (&(vh->allocationFile))->clumpSize );
  printf("TOTAL BLOCKS %lu\n",  (unsigned long) 
         (&(vh->allocationFile))->totalBlocks );
  printf("ALLOCATION BLOCK SIZE \t%lu\n", (unsigned long) vh->blockSize );
  printf("FREE BLOCKS \t\t%lu\n", (unsigned long) vh->freeBlocks );
  printf("TOTAL BLOCKS \t\t%lu\n", (unsigned long)vh->totalBlocks );
 
  /* ALLOCATE MEMORY FOR ALLOCATION BITMAP FILE */
  alloc_file_buf = (unsigned char *)malloc( 
           (&(vh->allocationFile))->logicalSize * sizeof(unsigned char) );
  if (alloc_file_buf == NULL)
  {
    printf("Error allocating array for allocation bitmap file.\n");
    exit(-1);
  }
 
  /* READ WHOLE ALLOCATION FILE INTO MEMORY AS ONE LARGE BUFFER */
  if (read_alloc_bitmap_into_mem(fd, vh, alloc_file_buf) == -1)
  {
    printf("Error reading extents into memory.\n");
    exit(-1);
  }
 
  /* NOW, LOOP THROUGH ALL UNALLOCATED SPACE 
   * AND LOOK FOR PNG HEADERS AND FOOTERS */ 
  uint64_t j;
  for (j = 0; j < (vh->freeBlocks * vh->blockSize); j++)
  {
    /* LOOK FOR PNG HEADER */
    if (j < (vh->freeBlocks * vh->blockSize) - png_header_size + 1)
    {
      if (unalloc_byte(fd, vh, alloc_file_buf, j) == png_header[0] && 
          unalloc_byte(fd, vh, alloc_file_buf, j+1) == png_header[1] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+2) == png_header[2] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+3) == png_header[3] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+4) == png_header[4] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+5) == png_header[5] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+6) == png_header[6] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+7) == png_header[7])
      {
        printf("PNG HEADER FOUND\n");
 
        file_found_flag = 1;
 
        snprintf (carved_file_name, 4096, "%s%llu%s", "carved_",
                                           carved_count, ".png");
 
        cfp = fopen(carved_file_name, "w");
        if (cfp == NULL)
        {
          printf("Carved output file could not be created.\n");
          exit(-1);
        }        
      }     
    }
 
    /* IF THE FILE FOUND FLAG IS SET, 
     * KEEP COPYING BYTES TO THE OUTPUT FILE */
    if (file_found_flag == 1)
    {
      fputc(unalloc_byte(fd, vh, alloc_file_buf, j), cfp);
    }
 
    /* LOOK FOR PNG FOOTER */
    if (j < (vh->freeBlocks * vh->blockSize) - png_footer_size + 1)
    {
      if (unalloc_byte(fd, vh, alloc_file_buf, j) == png_footer[0] && 
          unalloc_byte(fd, vh, alloc_file_buf, j+1) == png_footer[1] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+2) == png_footer[2] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+3) == png_footer[3] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+4) == png_footer[4] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+5) == png_footer[5] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+6) == png_footer[6] &&
          unalloc_byte(fd, vh, alloc_file_buf, j+7) == png_footer[7])
      {
        printf("PNG FOOTER FOUND\n");
 
        if (file_found_flag == 1)
        {
          /* WRITE FOOTER TO OUTPUT FILE */
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+1), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+2), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+3), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+4), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+5), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+6), cfp);
          fputc(unalloc_byte(fd, vh, alloc_file_buf, j+7), cfp);
          fclose(cfp);
 
          file_found_flag = 0;
          carved_count++;
        }
      }
    }
  }
 
  close(fd);
  free(alloc_file_buf);
 
  return 0;
}
