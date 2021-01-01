#include "ssufs-ops.h"

extern struct filehandle_t file_handle_array[MAX_OPEN_FILES];

int ssufs_allocFileHandle() { //사용하지 않는 filehandle 구하는 함수
	for(int i = 0; i < MAX_OPEN_FILES; i++) {
		if (file_handle_array[i].inode_number == -1) {
			return i;
		}
	}
	return -1;
}

int ssufs_create(char *filename){ //파일 생성
	/* 1 */
	int inodenum = 0; //할당한 inode 번호

	if(strlen(filename)>MAX_NAME_STRLEN) //파일이름이 최대 파일이름길이를 초과한 경우
		return -1;

	if(open_namei(filename)!=-1) //이미 동일한 파일이름이 존재한다면
		return -1;

	if((inodenum=ssufs_allocInode())==-1) //inode 공간이 없는지 확인
		return -1;

	struct inode_t *tmp = (struct inode_t *) malloc(sizeof(struct inode_t));
	memcpy(tmp->name, filename, sizeof(filename)); //이름 할당

	/* 초기화*/
	tmp->status = INODE_IN_USE; 
	for(int i =0; i<MAX_FILE_SIZE; i++) //datablock 초기화
		tmp->direct_blocks[i] = -1;
	tmp->file_size = 0;
	
	ssufs_writeInode(inodenum, tmp); //inode에 정보 쓰기
	free(tmp);
//	printf("create success\n");
	return inodenum;
}

void ssufs_delete(char *filename){ //파일 삭제
	/* 2 */
	int inodenum = open_namei(filename); //파일의 inode 구하기
	
	if(inodenum != -1){ //파일이 존재하는 경우
		ssufs_freeInode(inodenum); //해당 inode free
	}
//	printf("delete success\n");
}

int ssufs_open(char *filename){ //파일 오픈
	/* 3 */
	int inodenum=0;
	int file_handle=0;

	if((inodenum=open_namei(filename))==-1) //생성된 파일이 없다면
		return -1;

	if((file_handle=ssufs_allocFileHandle())==-1) //사용하지 않는 filehandle 구하기
		return -1;

	file_handle_array[file_handle].inode_number = inodenum; //오픈한 파일의 inode 할당
	file_handle_array[file_handle].offset = 0; //오픈한 파일의 offset 할당

//	printf("open success\n");
	return file_handle; //해당 file_handle 리턴
}

void ssufs_close(int file_handle){ //오픈한 파일 닫기
	file_handle_array[file_handle].inode_number = -1;
	file_handle_array[file_handle].offset = 0;
}

int ssufs_read(int file_handle, char *buf, int nbytes){ //파일 읽기
	/* 4 */
	if(file_handle_array[file_handle].inode_number == -1) //해당 파일이 open하지 않았으면 에러
		return -1;
	
	int offset = file_handle_array[file_handle].offset;
	struct inode_t *tmp = (struct inode_t *) malloc(sizeof(struct inode_t)); 
	ssufs_readInode(file_handle_array[file_handle].inode_number, tmp); //해당 파일의 inode 구조체 가져오기
	
	if(tmp->status == INODE_FREE) //해당 파일이 없는 경우 에러
		return -1;
	
	if(nbytes<1) //읽고자 하는 byte가 1보다 작다면 에러
		return -1;

	if(offset + nbytes > tmp->file_size) //요청된 nbytes를 읽으면 파일의 끝을 넘어가는 경우
		return -1;

	int blockindex = offset/BLOCKSIZE; //현재 offset이 있는 datablock 구하기
	int blocknum = 0;
	int readbytes = 0;
	char *data_buf = malloc(sizeof(char)*BLOCKSIZE);
	blocknum = tmp->direct_blocks[blockindex++]; //오프셋이 있는 데이터 블럭
	ssufs_readDataBlock(blocknum, data_buf); //해당 datablock 읽기
	
	for(int i= offset%BLOCKSIZE; i<BLOCKSIZE; i++){ //datablock 하나에 있는 data 읽기
		
		memcpy(buf+readbytes, data_buf+i, 1); //data 하나씩 읽기
		readbytes++;
		
		if(readbytes==nbytes){ //요청된 만큼 읽으면 return
			file_handle_array[file_handle].offset = offset + nbytes; //현재 offset 갱신
			free(tmp);
			free(data_buf);
			return 0;
		}
	}

	while(1){ //요청된 nbytes만큼 읽을 때까지 반복
		
		blocknum = tmp->direct_blocks[blockindex++]; //데이터 블럭 읽기
		ssufs_readDataBlock(blocknum, data_buf); //해당 datablock 읽기
		
		for(int i=0; i<BLOCKSIZE; i++){	
			memcpy(buf+readbytes, data_buf+i, 1); //data 하나씩 읽기
			readbytes++;
			
			if(readbytes==nbytes){ //요청된 만큼 읽으면 return
				file_handle_array[file_handle].offset = offset + nbytes; //현재 offset 갱신
				free(tmp);
				free(data_buf);
				return 0;
			}
		}
	}
}

int ssufs_write(int file_handle, char *buf, int nbytes){
	/* 5 */
	if(file_handle_array[file_handle].inode_number == -1) //해당 파일이 open하지 않았으면 에러
		return -1;

	int offset = file_handle_array[file_handle].offset;
	struct inode_t *inode = (struct inode_t*)malloc(sizeof(struct inode_t));
	ssufs_readInode(file_handle_array[file_handle].inode_number, inode);

	if(inode->status == INODE_FREE) //해당 파일이 없는 경우 에러
		return -1;

	if(nbytes<1) //쓰고자 하는 bytes가 1보다 작은 경우 에러
		return -1;

	if(inode->file_size + nbytes > BLOCKSIZE * MAX_FILE_SIZE) //요청된 바이트 수를 쓰면 최대 파일 크기를 초과하는 경우 
		return -1;

	int alloc_blocknum = 0;
	int total_blocknum = (nbytes+offset)/BLOCKSIZE; //nbytes를 할당한 뒤의 총 data_block 수
	int fsize = inode->file_size;

	if((nbytes+offset)%BLOCKSIZE > 0) //block을 하나 더 할당해야하는 경우
		total_blocknum++;
	
	for(int i = 0; i<total_blocknum; i++){ //free한 블록 확인
		if(inode->direct_blocks[i] == -1){
			if((inode->direct_blocks[i]=ssufs_allocDataBlock())==-1)
				break;
		}
		alloc_blocknum++;
	}

	if(total_blocknum != alloc_blocknum){ //쓰기를 완료하는데 필요한 free 블록이 부족한 경우

		int origin_block = offset/BLOCKSIZE; //기존의 할당되었던 data_block 수 구하기
		if(offset%BLOCKSIZE>0)
			origin_block++;

		for(int i=alloc_blocknum; i>origin_block; i--){ //이미 할당한 free 블록 해제
			ssufs_freeDataBlock(inode->direct_blocks[i-1]);
			inode->direct_blocks[i] = -1;
		}

		free(inode);
		return -1;
	}

	int writebytes = 0;
	int blockindex = offset/BLOCKSIZE; //현재 offset이 위치한 block
	int blocknum = inode->direct_blocks[blockindex++];
	char *data_buf = malloc(sizeof(char)*BLOCKSIZE);
	
	for(int i=0; i<BLOCKSIZE; i++) //메모리 초기화
		memcpy(&data_buf[i], "", 1);
	
	if(offset%BLOCKSIZE!=0)
		ssufs_readDataBlock(blocknum, data_buf); //해당 datablock 읽기

	for(int i=offset%BLOCKSIZE; i<BLOCKSIZE; i++){ //요청된 buf에 있는 데이터 하나씩 쓰기
		memcpy(data_buf+i, buf+writebytes, 1); //data 하나씩 쓰기
		writebytes++;

		if(writebytes==nbytes){ //요청된 만큼 쓰면 리턴
			file_handle_array[file_handle].offset = offset + nbytes; //offset 갱신
			inode->file_size = fsize + nbytes; //file_Size 갱신
			ssufs_writeDataBlock(blocknum, data_buf);
			ssufs_writeInode(file_handle_array[file_handle].inode_number, inode);
			free(inode);
			free(data_buf);
			return 0;
		}
	}

	ssufs_writeDataBlock(blocknum, data_buf); //offset이 위치한 datablock쓰기

	for(int i = blockindex; i<MAX_FILE_SIZE; i++){ //새로운 freedatablock 가져오기

		blocknum = inode->direct_blocks[i]; //할당받은 datablock
		
		for(int k=0; k<BLOCKSIZE; k++) //write한 버퍼의 메모리 초기화
			memcpy(&data_buf[k], "", 1);
		
		for(int j=0; j<BLOCKSIZE; j++){ //blocksize만큼 데이터 쓰기
			memcpy(data_buf+j, buf+writebytes, 1);
			writebytes++;

			if(writebytes==nbytes)
				break;
		}

		ssufs_writeDataBlock(blocknum, data_buf); //block단위로 데이터 쓰기

		if(writebytes==nbytes){ //요청된 만큼 쓰면 리턴
			file_handle_array[file_handle].offset = offset + nbytes; //offset 갱신
			inode->file_size = fsize + nbytes; //file_Size 갱신
			ssufs_writeInode(file_handle_array[file_handle].inode_number, inode);
			free(inode);
			free(data_buf);
			return 0;
		}
	}
}

int ssufs_lseek(int file_handle, int nseek){
	int offset = file_handle_array[file_handle].offset;

	struct inode_t *tmp = (struct inode_t *) malloc(sizeof(struct inode_t));
	ssufs_readInode(file_handle_array[file_handle].inode_number, tmp);
	
	int fsize = tmp->file_size;
	
	offset += nseek;

	if ((fsize == -1) || (offset < 0) || (offset > fsize)) {
		free(tmp);
		return -1;
	}

	file_handle_array[file_handle].offset = offset;
	free(tmp);

	return 0;
}
