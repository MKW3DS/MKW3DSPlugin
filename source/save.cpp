#include "save.hpp"
#include "main.hpp"
#include <3ds/util/utf.h>
#include <Unicode.h>

namespace CTRPluginFramework {
	archData OnionSave::save = {0};
	saveData OnionSave::settings = { 0 };
	char OnionSave::savePath[100] = {0};
	u16 OnionSave::romPath[50] = { 0 };
	u16 OnionSave::dataPath[50] = { 0 };
	File* OnionSave::debugFile = nullptr;
	bool OnionSave::needsReboot = false;

	LightLock debugLock = { 0 };
	Result OnionSave::loadDefaults() {
		settings.header.magic = SAVE_MAGIC;
		settings.header.version = SAVE_REVISION;
		settings.header.numEntries = 0;
		settings.header.lastLoadedPack = 0;
		return addModEntry(g_ProcessTID + 8, ARCH_ROMFS|ARCH_SAVE);
	}

	bool checkFolderExists(u16* name) {
		FS_Archive sdmcArchive;
		Handle dirHandle;
		Result ret = 0;
		ret = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
		if (ret) return false;
		ret = FSUSER_OpenDirectory(&dirHandle, sdmcArchive, fsMakePath(PATH_UTF16, name));
		FSDIR_Close(dirHandle);
		FSUSER_CloseArchive(sdmcArchive);
		if (ret) {
			return false;
		}
		return true;
	}

	bool createDirectory(char* name) {
		return Directory::Create(name) == 0;
	}

	void strcatu16(u16* dest, char* s1, char* s2) {
		while (*s1) *dest++ = *s1++;
		while (*s2) *dest++ = *s2++;
		*dest = '\0';
	}

	int strcmpu8u16(char* ptr1, u16* ptr2) {
		int i = 0;
		u16 char1;
		u16 char2;
		do {
			char1 = (u16)(ptr1[i]);
			char2 = ptr2[i++];
		} while (char2 == char1 && ptr1[i] && ptr2[i] != u':');
		return char1 - char2;
	}

	int strcmpdot(char* ptr1, char* ptr2) {
		int i = 0;
		char char1;
		char char2;
		do {
			char1 = ptr1[i];
			char2 = ptr2[i++];
		} while (char2 == char1 && ptr1[i] && ptr2[i] != ':');
		return char1 - char2;
	}

	void strcpydot(char* dst, char* src, int n) {
		while (*src != ':' && n) {
			*dst++ = *src++;
			n--;
		}
		*dst = '\0';
	}
#define PA_PTR(addr)            
	void OnionSave::initDebug() {
		if (ENABLE_DEBUG) {
			LightLock_Init(&debugLock);
			std::string debugfile = std::string(TOP_DIR"/") + std::string(g_ProcessTID) + std::string("debug.txt");
			File::Remove(debugfile);
			debugFile = new File(debugfile, File::RWC);
			if (!debugFile->IsOpen()) debugFile = nullptr;
			debugAppend("OnionFS " STRING_VERSION " debug file, hello! :3\n----------------------------------------\n\n");
		}
	}
	
	void OnionSave::debugAppend(std::string data) {
		if (!debugFile || !ENABLE_DEBUG) return;
		LightLock_Lock(&debugLock);
		debugFile->Write(data.c_str(), data.size());
		debugFile->Flush();
		LightLock_Unlock(&debugLock);
	}

	int OnionSave::existArchiveu16(u16 * arch)
	{
		for (int i = 0; i < save.numEntries; i++) {
			if (strcmpu8u16(save.entries[i].archName, arch) == 0) {
				if (save.entries[i].finished) return i;
				else return -1;
			}
		}
		return -1;
	}

	int OnionSave::existArchiveHnd(u64 hnd)
	{
		for (int i = 0; i < save.numEntries; i++) {
			if (save.entries[i].archHandle == hnd) {
				return i;
			}
		}
		return -1;
	}

	int OnionSave::existArchiveu8(u8 * arch)
	{
		for (int i = 0; i < save.numEntries; i++) {
			if (strcmpdot(save.entries[i].archName, (char*)arch) == 0) {
				if (save.entries[i].finished) return i;
				else return -1;
			}
		}
		return -1;
	}

	void OnionSave::addArchiveHnd(u64 handle, u32 archid) {
		if (archid != 4) {
			DEBUG("Rejected %016llX, archiveID: 0x%08X\n", handle, archid);
			return;
		}
		if (existArchiveHnd(handle) != -1) return;
		if (save.numEntries >= MAX_SAVE_ENTRIES) {
			DEBUG("Archive buffer is full.\n");
			return;
		}
		DEBUG("Adding save archive: %016lX\n", handle);
		save.entries[save.numEntries].type = ARCH_SAVE;
		save.entries[save.numEntries].archHandle = handle;
		save.entries[save.numEntries++].finished = 0;
	}

	void OnionSave::addArchive(u8* arch, u64 handle)
	{
		char newbuf[10];
		if (ENABLE_DEBUG) strcpydot(newbuf, (char*)arch, 10);
		if (arch[0] == '$') {
			DEBUG("Rejected \"%s\"\n", newbuf, (u32)(handle >> 32), (u32)handle);
			return; //Archives starting with $ are used by the game internally, usually mii data.
		}
		if (existArchiveu8(arch) == -1) {
			int hndpos = existArchiveHnd(handle);
			if (hndpos == -1) {
				if ((u32)(handle) > 0x100000 && (u32)(handle) < 0x20000000) { // rom archives doesn't have a fsarchive handle, it has a pointer in its place.
					DEBUG("Added \"%s\" as romfs archive!\n", newbuf);
					if (save.numEntries >= MAX_SAVE_ENTRIES) {
						DEBUG("Archive buffer is full.\n");
						return;
					}
					save.entries[save.numEntries].type = ARCH_ROMFS;
					save.entries[save.numEntries].archHandle = ~0x0;
					strcpydot((save.entries[save.numEntries].archName), (char*)arch, sizeof(save.entries[0].archName));
					save.entries[save.numEntries++].finished = 1;
					return;
				}
				else {
					DEBUG("Rejected \"%s\" with handle: 0x%08X%08X\n", newbuf, (u32)(handle >> 32), (u32)handle);
					return;
				}
			}
			DEBUG("Added \"%s\" with handle: 0x%08X%08X\n", newbuf, (u32)(handle >> 32), (u32)handle);
			if (save.entries[hndpos].finished == 1) return;
			strcpydot((save.entries[hndpos].archName), (char*)arch, sizeof(save.entries[0].archName));
			save.entries[hndpos].finished = 1;
		}
	}

	bool OnionSave::addModEntry(const char* name, u8 flags) {
		strcpy(settings.entries[settings.header.numEntries].name, name);
		settings.entries[settings.header.numEntries].flags = flags;
		char* dirName = static_cast<char *>(::operator new(0x200));
		if (!dirName) return false;
		//
		strcpy(dirName, (char*)TOP_DIR"/");
		strcat(dirName, name);
		createDirectory(dirName);
		//
		strcpy(dirName, (char*)TOP_DIR"/");
		strcat(dirName, name);
		strcat(dirName, (char*)"/gamefs");
		createDirectory(dirName);
		//
		strcpy(dirName, (char*)TOP_DIR"/");
		strcat(dirName, name);
		strcat(dirName, (char*)"/savefs");
		createDirectory(dirName);		
		delete[] dirName;
		settings.header.numEntries++;
		return true;
	}

	bool showMsgKbd(std::string text, DialogType digtype) {
		Keyboard kbd(text);
		StringVector opts;
		switch (digtype)
		{
		case CTRPluginFramework::DialogType::DialogOk:
			opts = { "Ok" };
			break;
		case CTRPluginFramework::DialogType::DialogOkCancel:
			opts = { "Ok", "Cancel" };
			break;
		case CTRPluginFramework::DialogType::DialogYesNo:
			opts = { "Yes", "No" };
			break;
		default:
			break;
		}
		kbd.Populate(opts);
		return kbd.Open() == 0;
	}

	void OnionSave::setupPackPaths() {
		strcatu16(OnionSave::romPath, (char*)"ram:" TOP_DIR"/", (char*)"gamefs/");
		strcatu16(OnionSave::dataPath, (char*)"ram:" TOP_DIR"/", (char*)"savefs/");
		if (!checkFolderExists(OnionSave::romPath + 4)) Directory::Create(TOP_DIR "/gamefs");
		if (!checkFolderExists(OnionSave::dataPath + 4)) Directory::Create(TOP_DIR "/savefs");
	}
	
	bool OnionSave::getArchive(u16 * arch, u8* mode, bool isReadOnly)
	{
		int entry = existArchiveu16(arch);
		if (entry == -1) return false;
		u8 flag = save.entries[entry].type;
		*mode = flag;
		u8 romode = !((flag & ARCH_ROMFS) && isReadOnly); //Only allow functions without the readonly flag
		return romode;
	}

	bool OnionSave::loadSettings() {
		return loadDefaults();
	}
}