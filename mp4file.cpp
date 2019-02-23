// mp4file.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <memory.h>
#include <map>
#include <set>
#include <list>

/*
** ffmpeg 在mov.c中有对mp4文件解析的代码
*/

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#define MKBETAG(a,b,c,d) ((d) | ((c) << 8) | ((b) << 16) | ((unsigned)(a) << 24))

#define BOX_TAG_FTYP	MKTAG('f','t','y','p')
#define BOX_TAG_MOOV	MKTAG('m','o','o','v')
#define BOX_TAG_MDAT	MKTAG('m','d','a','t')

//moov sub box
#define MOOV_BOX_TAG_MVHD	MKTAG('m','v','h','d')
#define MOOV_BOX_TAG_TRAK	MKTAG('t','r','a','k')
#define MOOV_BOX_TAG_UDTA	MKTAG('u','d','t','a')

#define MAX_MVHD_SIZE   120

//big endian byte order ,read 4 bytes
unsigned long long ReadInt8BE(const unsigned char* buf)
{
	if (NULL == buf)
	{
		return 0;
	}
	unsigned long long val = ((unsigned long long)buf[0] << 56) | ((unsigned long long)buf[1] << 48)		\
		| ((unsigned long long)buf[2] << 40) | ((unsigned long long)buf[3] << 32) | ((unsigned long long)buf[4] << 24) \
		| ((unsigned long long)buf[5] << 16) | ((unsigned long long)buf[6] << 8) | ((unsigned long long)buf[7]);

	return val;
}

//big endian byte order ,read 4 bytes
unsigned int ReadInt4BE(const unsigned char* buf)
{
	if (NULL == buf)
	{
		return 0;
	}
	unsigned int val = ((unsigned int)buf[0] << 24) | ((unsigned int)buf[1] << 16)		\
		| ((unsigned int)buf[2] << 8) | ((unsigned int)buf[3]);

	return val;
}

//big endian byte order ,read 3 bytes
unsigned int ReadInt3BE(const unsigned char* buf)
{
	if (NULL == buf)
	{
		return 0;
	}
	unsigned int val = ((unsigned int)buf[0] << 16) | ((unsigned int)buf[1] << 8)		\
		| ((unsigned int)buf[2]);

	return val;
}

//big endian byte order ,read 2 bytes
unsigned short ReadInt2BE(const unsigned char* buf)
{
	if (NULL == buf)
	{
		return 0;
	}
	unsigned short val = ((unsigned short)buf[0] << 8) | ((unsigned short)buf[1]);

	return val;
}

//big endian byte order ,read 1 bytes
unsigned char ReadInt1(const unsigned char* buf)
{
	if (NULL == buf)
	{
		return 0;
	}
	unsigned char val = ((unsigned char)buf[0]);

	return val;
}

void ReadBoxType(const unsigned char* buf,char* dst)
{
	if (NULL == buf || NULL == dst)
	{
		return ;
	}
	memcpy(dst,buf,4);
	dst[4] = '\0';
}

std::string Tag2String(unsigned int tag)
{
	char lszTag[5] = { 0 };
	memcpy(lszTag,(char*)&tag,4);

	return std::string(lszTag);
}

struct BoxHead
{
	unsigned long long		m_size;
	char					m_type[5];
	unsigned long long		m_bodyOffset;

	BoxHead()
	{
		m_size = 0;
		memset(m_type,0,sizeof(m_type));
		m_bodyOffset = 0;
	}
};

typedef std::list<struct BoxHead> BoxHeadList;

/*
* 
aligned(8) class FileTypeBox extends Box(‘ftyp’) 
{ 
    unsigned int(32) major_brand; 
    unsigned int(32) minor_version; 
    unsigned int(32) compatible_brands[];  // to end of the box 
}
*/
struct FtypInfo
{
	unsigned int			m_majorBrand;
	unsigned int			m_minorVersion;
	std::set<unsigned int>	m_compatibleBrands;

	FtypInfo()
	{
		m_majorBrand = 0;
		m_minorVersion = 0;
	}

	std::string DumpFtypInfo()
	{
		std::string str;
		char lszInfo[256] = { 0 };
		sprintf(lszInfo,"major brand: %s, minor version: 0x%x, compatible brands:",
			Tag2String(m_majorBrand).c_str(), m_minorVersion);
		str = lszInfo;
		std::set<unsigned int>::iterator iter = m_compatibleBrands.begin();
		for (; iter != m_compatibleBrands.end(); iter++)
		{
			std::string sTag = Tag2String(*iter).append(" ");
			str.append(sTag);
		}
		return str;
	}
};

struct MvhdInfo
{
	//FullBox，是Box的扩展，Box结构的基础上在Header中增加8bits version和24bits flags
	unsigned char		m_version;		//8bits version
	unsigned int		m_flags;		//24bits flags

	/*
	*
		aligned(8) class MovieHeaderBox extends FullBox(‘mvhd’, version, 0) 
		{ 
			if (version==1) 
			{ 
				unsigned int(64) creation_time; 
				unsigned int(64) modification_time; 
				unsigned int(32) timescale; 
				unsigned int(64) duration; 
			} 
			else 
			{ // version==0 
				unsigned int(32) creation_time; 
				unsigned int(32) modification_time; 
				unsigned int(32) timescale; 
				unsigned int(32) duration; 
			} 
			template int(32)  rate = 0x00010000; // typically 1.0 
			template int(16)  volume = 0x0100;  // typically, full volume 
			const bit(16)  reserved = 0; 
			const unsigned int(32)[2]  reserved = 0; 
			template int(32)[9]  matrix = { 0x00010000,0,0,0,0x00010000,0,0,0,0x40000000 }; 
			// Unity matrix 
			bit(32)[6]  pre_defined = 0; 
			unsigned int(32) next_track_ID; 
		} 
	*/
	//time is an integer that declares the creation time of the presentation (in seconds since midnight, Jan. 1, 1904, in UTC time) 
	unsigned long long		m_creationTime;
	unsigned long long		m_modificationTime;
	//timescale，该数值表示本文件的所有时间描述所采用的单位。0x3E8 = 1000，即将1s平均分为1000份，每份1ms
	unsigned int			m_timeScale;
	//duration，媒体可播放时长，0xA06A2 =  657058，这个数值的单位与实际时间的对应关系就要通过上面的timescale参数。
	//duration / timescale = 可播放时长（s）
	unsigned long long		m_duration;

	//0x00010000 媒体速率，这个值代表原始倍速。
	int						m_rate;

	//0x0100 媒体音量，这个值代表满音量
	short					m_volume;

	std::string DumpInfo()
	{
		std::string str;
		char lszInfo[1024] = { 0 };
		sprintf(lszInfo,"version: 0x%x, flags: 0x%x, creationTime: 0x%llx, modificationTime: 0x%llx , timescale: %d,duration: %llds, rate; 0x%x, volume: 0x%x",
			m_version, m_flags, m_creationTime, m_modificationTime, m_timeScale, m_duration, m_rate, m_volume);
		str = lszInfo;
		
		return str;
	}
};

/*
* trak——This is a container box for a single track of a presentation. 
* A presentation consists of one or more tracks. 
* Each track is independent of the other tracks in the presentation and carries its own temporal and spatial information.
* Each track will contain its associated Media Box.
*/
struct TrakInfo
{
	std::map<unsigned int,BoxHeadList>	m_boxIndex;
};

typedef std::list<struct TrakInfo> TrakInfoList;

/*
*moov包含的一系列次级box中存储着媒体播放所需的元数据（metadata）
*/
struct MoovInfo
{
	std::map<unsigned int,BoxHeadList>	m_boxIndex;

	struct MvhdInfo						m_mvhdInfo;

	TrakInfoList						m_trakInfoList;
};

void DumpBoxHeader(const struct BoxHead& bh)
{
	printf("BoxType : %s, BoxSize : %llu, box body offset:%llu, box body size: %llu\n",
		bh.m_type, bh.m_size, bh.m_bodyOffset, bh.m_size - 8);
}

void ParseFtypBox(const unsigned char* body,const unsigned int size, struct FtypInfo& info)
{
	if (body == NULL || size == 0)
	{
		return;
	}
	unsigned int nLeftSize = size;
	const unsigned char* ptr = body;
	if (nLeftSize >= 4)
	{
		info.m_majorBrand = MKTAG(ptr[0],ptr[1],ptr[2],ptr[3]);
		ptr += 4;
		nLeftSize -= 4;
	}
	if (nLeftSize >= 4)
	{
		info.m_minorVersion = ReadInt4BE(ptr);
		ptr += 4;
		nLeftSize -= 4;
	}
	while (nLeftSize >= 4)
	{
		unsigned int brand = MKTAG(ptr[0],ptr[1],ptr[2],ptr[3]);
		info.m_compatibleBrands.insert(brand);
		ptr += 4;
		nLeftSize -= 4;
	}
}

void ParseAllBox(FILE* fp, const unsigned long long bodyOffset, unsigned long long bodySize, std::map<unsigned int,BoxHeadList> &boxIndex)
{
	if (NULL == fp)
	{
		return ;
	}
	unsigned bodyEnd = bodyOffset + bodySize;
	if (bodySize == (unsigned long long)-1)
	{
		bodyEnd = bodySize;
	}
	_fseeki64(fp,bodyOffset,SEEK_SET);
	unsigned char lszBoxHeader[8] = { 0 };
	int nRead = fread(lszBoxHeader,1,8,fp);
	
	while (nRead > 0)
	{
		BoxHead boxHead;
		if (nRead >= 8)
		{
			boxHead.m_size = ReadInt4BE(lszBoxHeader);
			ReadBoxType(lszBoxHeader + 4,boxHead.m_type);
			boxHead.m_bodyOffset = _ftelli64(fp);
			DumpBoxHeader(boxHead);
			unsigned int tag = MKTAG(boxHead.m_type[0],boxHead.m_type[1],boxHead.m_type[2],boxHead.m_type[3]);
			if (boxIndex.find(tag) == boxIndex.end())
			{
				BoxHeadList boxList;
				boxList.push_back(boxHead);
				boxIndex[tag] = boxList;
			}
			else
			{
				boxIndex[tag].push_back(boxHead);
			}
			if (boxHead.m_bodyOffset + boxHead.m_size - 8 >= bodyEnd)
			{
				break;
			}
			if (boxHead.m_size >= 8)
			{
				_fseeki64(fp,boxHead.m_size - 8,SEEK_CUR);
				nRead = fread(lszBoxHeader,1,8,fp);
			}
			else if (boxHead.m_size == 0)
			{
				printf("read the end box\n");
				break;
			}
			else if (boxHead.m_size == 1)
			{
				printf("read large box\n");
				break;
			}
			else
			{
				printf("read invalid box size : %d\n",boxHead.m_size);
				break;
			}
		}
		else
		{
			printf("Read box head fail, read bytes : %d",nRead);
			break;
		}
	}
}

/*
* 全文件唯一的（一个文件中只能包含一个mvhd box），对整个文件所包含的媒体数据作全面的全局的描述。
* 包含了媒体的创建与修改时间时间刻度、默认音量、色域、时长等信息。
*/
void ParseMvhd(FILE *fp, const unsigned long long bodyOffset, unsigned long long bodySize, struct MvhdInfo &info)
{
	if (NULL == fp || 0 == bodySize || bodySize > MAX_MVHD_SIZE)
	{
		return;
	}
	unsigned bodyEnd = bodyOffset + bodySize;
	_fseeki64(fp,bodyOffset,SEEK_SET);

	unsigned char* pBuf = new(std::nothrow) unsigned char[bodySize];
	if (pBuf)
	{
		fread(pBuf,1,bodySize,fp);
		const unsigned char* ptr = pBuf;
		unsigned int nLeftSize = bodySize;
		if (nLeftSize >= 1)
		{
			info.m_version = ReadInt1(ptr);
			ptr += 1;
			nLeftSize -= 1;
		}
		if (nLeftSize >= 3)
		{
			info.m_flags = ReadInt3BE(ptr);
			ptr += 3;
			nLeftSize -= 3;
		}
		if (info.m_version == 1)
		{
			if (nLeftSize >= 8)
			{
				info.m_creationTime = ReadInt8BE(ptr);
				ptr += 8;
				nLeftSize -= 8;
			}
			if (nLeftSize >= 8)
			{
				info.m_modificationTime = ReadInt8BE(ptr);
				ptr += 8;
				nLeftSize -= 8;
			}
			if (nLeftSize >= 4)
			{
				info.m_timeScale = ReadInt4BE(ptr);
				ptr += 4;
				nLeftSize -= 4;
			}
			if (nLeftSize >= 8)
			{
				info.m_duration = ReadInt8BE(ptr);
				ptr += 8;
				nLeftSize -= 8;
			}
		}
		else if (info.m_version == 0)
		{
			if (nLeftSize >= 4)
			{
				info.m_creationTime = ReadInt4BE(ptr);
				ptr += 4;
				nLeftSize -= 4;
			}
			if (nLeftSize >= 4)
			{
				info.m_modificationTime = ReadInt4BE(ptr);
				ptr += 4;
				nLeftSize -= 4;
			}
			if (nLeftSize >= 4)
			{
				info.m_timeScale = ReadInt4BE(ptr);
				ptr += 4;
				nLeftSize -= 4;
			}
			if (nLeftSize >= 4)
			{
				info.m_duration = ReadInt4BE(ptr);
				ptr += 4;
				nLeftSize -= 4;
			}
		}
		else
		{
			printf("Invalid mvhd version ; %d",info.m_version);
			return;
		}
		if (nLeftSize >= 4)
		{
			info.m_rate = ReadInt4BE(ptr);
			ptr += 4;
			nLeftSize -= 4;
		}
		if (nLeftSize >= 2)
		{
			info.m_volume = ReadInt2BE(ptr);
			ptr += 2;
			nLeftSize -= 2;
		}
	}
}

void ParseTrak(FILE *fp, const unsigned long long bodyOffset, unsigned long long bodySize, struct TrakInfo &info)
{
	if (NULL == fp || bodyOffset == 0 || bodySize == 0)
	{
		return ;
	}

	printf("\nparse trak box begin:\n");
	ParseAllBox(fp, bodyOffset, bodySize, info.m_boxIndex);
	printf("parse trak box end\n");
}

void ParseMoovBox(FILE* fp, const unsigned long long bodyOffset, unsigned long long bodySize, struct MoovInfo& info)
{
	if (NULL == fp || bodyOffset == 0 || bodySize == 0)
	{
		return ;
	}

	printf("\nparse moov box begin:\n");
	ParseAllBox(fp, bodyOffset, bodySize, info.m_boxIndex);
	printf("parse moov box end\n");

	if (info.m_boxIndex.find(MOOV_BOX_TAG_MVHD) != info.m_boxIndex.end())
	{
		for (BoxHeadList::iterator lstInter = info.m_boxIndex[MOOV_BOX_TAG_MVHD].begin(); 
			lstInter != info.m_boxIndex[MOOV_BOX_TAG_MVHD].end(); lstInter++)
		{
			printf("\nparse mvhd box begin:\n");
			ParseMvhd(fp,lstInter->m_bodyOffset,lstInter->m_size - 8,info.m_mvhdInfo);

			std::string sInfo = info.m_mvhdInfo.DumpInfo();
			printf("mvhd info : %s\n",sInfo.c_str());

			printf("parse mvhd box end\n");
		}
	}

	if (info.m_boxIndex.find(MOOV_BOX_TAG_TRAK) != info.m_boxIndex.end())
	{
		for (BoxHeadList::iterator lstInter = info.m_boxIndex[MOOV_BOX_TAG_TRAK].begin(); 
			lstInter != info.m_boxIndex[MOOV_BOX_TAG_TRAK].end(); lstInter++)
		{
			printf("\nparse trak box begin:\n");
			TrakInfo trakInfo;
			ParseTrak(fp,lstInter->m_bodyOffset,lstInter->m_size - 8,trakInfo);

			info.m_trakInfoList.push_back(trakInfo);

			printf("parse trak box end\n");
		}
	}
}

void AddBoxIndex(std::map<unsigned int,BoxHead> &boxIndex,const struct BoxHead& bh)
{
	unsigned int tag = MKTAG(bh.m_type[0],bh.m_type[1],bh.m_type[2],bh.m_type[3]);
	switch (tag)
	{
	case BOX_TAG_FTYP:
	case BOX_TAG_MOOV:
	case BOX_TAG_MDAT:
		{
			boxIndex[tag] = bh;
		}
		break;
	default:
		break;
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	FILE *fp = fopen("c:\\test.mp4","rb");
	if (NULL == fp)
	{
		printf("open file failn");
		return -1;
	}

	std::map<unsigned int,BoxHeadList> boxIndex;
	ParseAllBox(fp,0,(unsigned long long)-1,boxIndex);

	int nRead = 0;
	FtypInfo ftypInfo;
	if (boxIndex.find(BOX_TAG_FTYP) != boxIndex.end())
	{
		for (BoxHeadList::iterator lstInter = boxIndex[BOX_TAG_FTYP].begin(); 
			lstInter != boxIndex[BOX_TAG_FTYP].end(); lstInter++)
		{
			printf("\nparse ftyp box begin\n");
			int nFtypSize = lstInter->m_size - 8;
			_fseeki64(fp,lstInter->m_bodyOffset,SEEK_SET);
			unsigned char *pBuf = new(std::nothrow) unsigned char[nFtypSize];
			if (pBuf)
			{
				nRead = fread(pBuf,1,nFtypSize,fp);
				if (nRead == nFtypSize)
				{
					ParseFtypBox(pBuf,nRead,ftypInfo);
					std::string dumpstr = ftypInfo.DumpFtypInfo();
					printf("ftyp box info : %s\n",dumpstr.c_str());
				}
				delete []pBuf;
				pBuf = NULL;
			}
			printf("parse ftyp box end\n");
		}
	}

	MoovInfo moovInfo;
	if (boxIndex.find(BOX_TAG_MOOV) != boxIndex.end())
	{
		for (BoxHeadList::iterator lstInter = boxIndex[BOX_TAG_MOOV].begin(); 
			lstInter != boxIndex[BOX_TAG_MOOV].end(); lstInter++)
		{
			ParseMoovBox(fp,lstInter->m_bodyOffset,lstInter->m_size - 8,moovInfo);
		}
	}
	fclose(fp);
	fp = NULL;

	getchar();

	return 0;
}

