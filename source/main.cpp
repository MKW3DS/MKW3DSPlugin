#include <CTRPluginFramework.hpp>
#include "main.hpp"
#include "save.hpp"
#include "ptm.h"
#include "Lang.hpp"

u8 fsMountArchivePat1[] = { 0x10, 0x00, 0x97, 0xE5, 0xD8, 0x20, 0xCD, 0xE1, 0x00, 0x00, 0x8D };
u8 fsMountArchivePat2[] = { 0x28, 0xD0, 0x4D, 0xE2, 0x00, 0x40, 0xA0, 0xE1, 0xA8, 0x60, 0x9F, 0xE5, 0x01, 0xC0, 0xA0, 0xE3 };
u8 fsRegArchivePat[] = { 0xB4, 0x44, 0x20, 0xC8, 0x59, 0x46, 0x60, 0xD8 };
u8 userFsTryOpenFilePat1[] = { 0x0D, 0x10, 0xA0, 0xE1, 0x00, 0xC0, 0x90, 0xE5, 0x04, 0x00, 0xA0, 0xE1, 0x3C, 0xFF, 0x2F, 0xE1 };
u8 userFsTryOpenFilePat2[] = { 0x10, 0x10, 0x8D, 0xE2, 0x00, 0xC0, 0x90, 0xE5, 0x05, 0x00, 0xA0, 0xE1, 0x3C, 0xFF, 0x2F, 0xE1 };
u8 openArchivePat[] = { 0xF0, 0x81, 0xBD, 0xE8, 0xC2, 0x00, 0x0C, 0x08 };
u8 formatSavePat[] = { 0xF0, 0x9F, 0xBD, 0xE8, 0x42, 0x02, 0x4C, 0x08 };
u8 fsSetThisSecValPat[] = {0xC0, 0x00, 0x6E, 0x08};
u8 fsObsSetThisSecValPat[] = {0x40, 0x01, 0x65, 0x08};
u8 fsSetSecValPat[] = {0x80, 0x01, 0x75, 0x08};
u8 fsCheckPermsPat[] = { 0x04, 0x10, 0x12, 0x00, 0x76, 0x46, 0x00, 0xD9 };

u8 msbtConsPat[] = { 0x25, 0x73, 0x2E, 0x6D, 0x73, 0x62, 0x74, 0x00};
u32 g_msbtconstret = 0x100000;

namespace CTRPluginFramework
{
    bool ENABLE_DEBUG = false;

    u32* fileOperations[NUMBER_FILE_OP] = { nullptr };
    RT_HOOK fileOpHooks[NUMBER_FILE_OP] = { 0 };
    RT_HOOK	regArchiveHook = { 0 };
    RT_HOOK openArchiveHook = { 0 };
    RT_HOOK formatSaveHook = { 0 };
    RT_HOOK fsSetThisSecValHook = { 0 };
    RT_HOOK fsObsSetThisSecValHook = { 0 };
    RT_HOOK fsSetSecValHook = { 0 };
    RT_HOOK getmsbtptrfromobjhook = { 0 };
    u32 fsMountArchive = 0;
    char g_ProcessTID[17];

    void mcuSetSleep(bool on){
        u8 reg;
        if (R_FAILED(mcuHwcInit())) return;
        if (R_FAILED(MCUHWC_ReadRegister(0x18, &reg, 1))) goto exit;
        if (on)
            reg &= ~0x6C;
        else
            reg |= 0x6C;
        MCUHWC_WriteRegister(0x18, &reg, 1);
        exit:
        mcuHwcExit();
        return;
    }

    void deleteSecureVal() {
        DEBUG("NOTE: This game uses a secure value, ");
        Result res;
        u8 out;
        u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (((u32)Process::GetTitleID() >> 8) << 8);
        res = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
        if (res) {
            DEBUG(" fsControlSecureSave returned: 0x%08X, proceeding to patch fs", res);
            Handle prochand;
            res = svcOpenProcess(&prochand, 0); //fs processID
            if (res) {
                DEBUG(", svcOpenProcess returned: 0x%08X, aborting.\n", res); 
                customBreak(0xAB047, 1, 0);
            }
            s64 info;
            res = svcGetProcessInfo(&info, prochand, 0x10005); //get start of .text
            if (res) {
                DEBUG(", svcGetProcessInfo 0x10005 returned: 0x%08X, aborting.\n", res);
                customBreak(0xAB047, 1, 0);
            }
            u32* addr = (u32*)info;
            res = svcGetProcessInfo(&info, prochand, 0x10002); //get .text size
            if (res) {
                DEBUG(", svcGetProcessInfo 0x10002 returned: 0x%08X, aborting.\n", res);
                customBreak(0xAB047, 1, 0);
            }
            res = svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x08000000, prochand, (u32)addr, (u32)info);
            if (res) {
                DEBUG(", svcMapProcessMemoryEx returned: 0x%08X, aborting.\n", res);
                customBreak(0xAB047, 1, 0);
            }
            addr = (u32*)0x08000000;
            u32* endAddr = (u32*)((u32)addr + (u32)info);
            std::vector<u32*> backup;
            DEBUG(" (patched : ");
            bool first = true;
            while (addr < endAddr) {
                if (memcmp(addr, fsCheckPermsPat, sizeof(fsCheckPermsPat)) == 0) {
                    backup.push_back(addr);
                    *addr = 0x80; //SD access patched by Luma3DS
                    if (first) {
                        DEBUG("0x%08X", (u32)addr);
                        first = false;
                    } else 	DEBUG(", 0x%08X", (u32)addr);
                }
                addr++;
            }
            DEBUG("), ");
            svcInvalidateEntireInstructionCache();
            res = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
            if (res) {
                DEBUG("patched fsControlSecureSave returned: 0x%08X, abort.\n", res);
                customBreak(0xAB047, 1, 0);
            }
            else {
                DEBUG("patch succeeded, ");
            }
            for (u32 *addrRest : backup)
                *addrRest = 0x121004;
            svcInvalidateEntireInstructionCache();
            svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x08000000, (u32)info);
            svcCloseHandle(prochand);
        }
        if (out) {
            DEBUG("secure value has been deleted.\n");
        }
        else {
            DEBUG("but there was no secure value stored.\n");
        }
    }

    u32* findNearestSTMFD(u32* newaddr) {
        u32 i;
        for (i = 0; i < 1024; i++) {
            newaddr--;
            i++;
            if (*((u16*)newaddr + 1) == 0xE92D) {
                return newaddr;
            }
        }
        return 0;
    }

    static inline u32   decodeARMBranch(const u32 *src)
    {
        s32 off = (*src & 0xFFFFFF) << 2;
        off = (off << 6) >> 6; // sign extend

        return (u32)src + 8 + off;
    }

    void storeAddrByOffset(u32* addr, u16 offset) {
        if (offset % 4 != 0) return;
        offset >>= 2;
        if (ENABLE_DEBUG) {
            char* funcstr = (char*)"";
            char buf[10];
            switch (offset) {
            case 0:
                funcstr = (char*)"fsOpenFile";
                break;
            case 1:
                funcstr = (char*)"fsOpenDirectory";
                break;
            case 2:
                funcstr = (char*)"fsDeleteFile";
                break;
            case 3:
                funcstr = (char*)"fsRenameFile";
                break;
            case 4:
                funcstr = (char*)"fsDeleteDirectory";
                break;
            case 5:
                funcstr = (char*)"fsDeleteDirectoryRecursive";
                break;
            case 6:
                funcstr = (char*)"fsCreateFile";
                break;
            case 7:
                funcstr = (char*)"fsCreateDirectory";
                break;
            case 8:
                funcstr = (char*)"fsRenameDirectory";
                break;
            default:
                snprintf(buf, sizeof(buf), "%d", offset);
                funcstr = buf;
            }
            DEBUG("> %s found at 0x%08X\n", funcstr, (u32)addr);
        }
        if (offset < NUMBER_FILE_OP) fileOperations[offset] = addr;
    }

    void processFileSystemOperations(u32* funct, u32* endAddr) {
        DEBUG("\nStarting to process fs functions...\n");
        int i;
        for (i = 0; i < 0x20; i++) { // Search for the closest BL, this BL will branch to getArchObj
            if ((*(funct + i) & 0xFF000000) == 0xEB000000) {
                funct += i;
                break;
            }
        }
        u32 funcAddr;
        u32* addr;
        int ctr = 1;
        if (i >= 0x20) { // If there are no branches, the function couldn't be found.
            DEBUG("> ERROR: Couldn't find getArchObj\n");
            ctr = 0;
            goto exit;
        }
        funcAddr = decodeARMBranch(funct); // Get the address of getArchObj
        DEBUG("> getArchObj found at 0x%08X\n", funcAddr);
        addr = (u32*)0x100000;
        while (addr < endAddr) { // Scan the text section of the code for the fs functions
            if ((*addr & 0xFF000000) == 0xEB000000 && (decodeARMBranch(addr) == funcAddr)) { //If a branch to getArchObj if found analize it.
                u8 regId = 0xFF;
                for (i = 0; i < 1024; i++) { //Scan forwards for the closest BLX, and get the register it is branching to
                    int currinst = addr[i];
                    if (*((u16*)(addr + i) + 1) == 0xE92D) break; //Stop if STMFD is found (no BLX in this function)
                    if ((currinst & ~0xF) == 0xE12FFF30) { //BLX
                        regId = currinst & 0xF;
                        break;
                    }
                }
                if (regId != 0xFF) { // If a BLX is found, scan backwards for the nearest LDR to the BLX register.
                    int j = i;
                    for (; i > 0; i--) {
                        if (((addr[i] & 0xFFF00000) == 0xE5900000) && (((addr[i] & 0xF000) >> 12) == regId)) { //If it is a LDR and to the BLX register
                            storeAddrByOffset(findNearestSTMFD(addr), addr[i] & 0xFFF); //It is a fs function, store it based on the LDR offset. (This LDR gets the values from the archive object vtable, by checking the vtable offset it is possible to know which function it is)
                            break;
                        }
                    }
                    addr += j; // Continue the analysis from the BLX
                }
            }
            addr++;
        }
        for (int i = 0; i < NUMBER_FILE_OP; i++) {
            if (fileOperations[i] == nullptr) continue;
            ctr++;
            rtInitHook(&fileOpHooks[i], (u32)fileOperations[i], fileOperationFuncs[i]);
            rtEnableHook(&fileOpHooks[i]);
        }
        exit:
        DEBUG("Finished processing fs functions: %d/%d found.\n\n", ctr, NUMBER_FILE_OP + 1);
    }

    void initOnionFSHooks(u32 textSize) {
        u32* addr = (u32*)0x100000;
        u32* endAddr = (u32*)(0x100000 + textSize);
        bool contOpen = true, contMount = true, contReg = true, contArch = true, contDelete = true, contSetThis = true, contSetObs = true, contSet = true, langSet = true;
        while (addr < endAddr && (contOpen || contMount || contReg || contArch || contDelete || contSetThis || contSetObs || contSet || langSet)) {
            if (contOpen && memcmp(addr, userFsTryOpenFilePat1, sizeof(userFsTryOpenFilePat1)) == 0 || memcmp(addr, userFsTryOpenFilePat2, sizeof(userFsTryOpenFilePat2)) == 0) {
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("tryOpenFile found at 0x%08X\n", (u32)fndaddr);
                contOpen = false;
                processFileSystemOperations(fndaddr, endAddr);
            }
            if (contMount && memcmp(addr, fsMountArchivePat1, sizeof(fsMountArchivePat1)) == 0 || memcmp(addr, fsMountArchivePat2, sizeof(fsMountArchivePat2)) == 0) {
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("mountArchive found at 0x%08X\n", (u32)fndaddr);
                contMount = false;
                fsMountArchive = (u32)fndaddr;
            }
            if (contReg && memcmp(addr, fsRegArchivePat, sizeof(fsRegArchivePat)) == 0) {
                contReg = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("registerArchive found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&regArchiveHook, (u32)fndaddr, (u32)fsRegArchiveCallback);
                rtEnableHook(&regArchiveHook);
            }
            if (contArch && memcmp(addr, openArchivePat, sizeof(openArchivePat)) == 0) {
                contArch = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("openArchive found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&openArchiveHook, (u32)fndaddr, (u32)fsOpenArchiveFunc);
                rtEnableHook(&openArchiveHook);
            }
            if (contDelete && memcmp(addr, formatSavePat, sizeof(formatSavePat)) == 0) {
                contDelete = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("formatSaveData found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&formatSaveHook, (u32)fndaddr, (u32)fsFormatSaveData);
                rtEnableHook(&formatSaveHook);
            }
            if (contSetThis && memcmp(addr, fsSetThisSecValPat, sizeof(fsSetThisSecValPat)) == 0) {
                contSetThis = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("fsSetThisSaveDataSecureValue found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&fsSetThisSecValHook, (u32)fndaddr, (u32)fsSetThisSaveDataSecureValue);
                rtEnableHook(&fsSetThisSecValHook);
            }
            if (contSetObs && memcmp(addr, fsObsSetThisSecValPat, sizeof(fsObsSetThisSecValPat)) == 0) {
                contSetObs = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("Obsoleted_5_0_fsSetSaveDataSecureValue found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&fsObsSetThisSecValHook, (u32)fndaddr, (u32)Obsoleted_5_0_fsSetSaveDataSecureValue);
                rtEnableHook(&fsObsSetThisSecValHook);
            }
            if (contSet && memcmp(addr, fsSetSecValPat, sizeof(fsSetSecValPat)) == 0) {
                contSet = false;
                u32* fndaddr = findNearestSTMFD(addr);
                DEBUG("fsSetSaveDataSecureValue found at 0x%08X\n", (u32)fndaddr);
                rtInitHook(&fsSetSecValHook, (u32)fndaddr, (u32)fsSetSaveDataSecureValue);
                rtEnableHook(&fsSetSecValHook);
            }
            if (langSet && memcmp(addr, msbtConsPat, sizeof(msbtConsPat)) == 0) {
                langSet = false;
                rtInitHook(&getmsbtptrfromobjhook, (u32)addr - 0x48, (u32)msbtconstfunc);
                g_msbtconstret = (u32)addr - 0x40;
                rtEnableHook(&getmsbtptrfromobjhook);
            }
            addr++;
        }
        if (fsSetThisSecValHook.isEnabled || fsObsSetThisSecValHook.isEnabled || fsSetSecValHook.isEnabled) {
            deleteSecureVal();
        }
        if (!(formatSaveHook.isEnabled && openArchiveHook.isEnabled && regArchiveHook.isEnabled)) {
            DEBUG("ERROR: Some hooks couldn't be initialized, aborting.\n");
            customBreak(0xab047, 0, 0);
        }
    }

    // This patch the NFC disabling the touchscreen when scanning an amiibo, which prevents ctrpf to be used
    static void    ToggleTouchscreenForceOn(void)
    {
        static u32 original = 0;
        static u32 *patchAddress = nullptr;

        if (patchAddress && original)
        {
            *patchAddress = original;
            return;
        }

        static const std::vector<u32> pattern =
        {
            0xE59F10C0, 0xE5840004, 0xE5841000, 0xE5DD0000,
            0xE5C40008, 0xE28DD03C, 0xE8BD80F0, 0xE5D51001,
            0xE1D400D4, 0xE3510003, 0x159F0034, 0x1A000003
        };

        Result  res;
        Handle  processHandle;
        s64     textTotalSize = 0;
        s64     startAddress = 0;
        u32 *   found;

        if (R_FAILED(svcOpenProcess(&processHandle, 16)))
            return;

        svcGetProcessInfo(&textTotalSize, processHandle, 0x10002);
        svcGetProcessInfo(&startAddress, processHandle, 0x10005);
        if(R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x14000000, processHandle, (u32)startAddress, textTotalSize)))
            goto exit;

        found = (u32*)Utils::Search<u32>(0x14000000, (u32)textTotalSize, pattern);

        if (found != nullptr)
        {
            original = found[13];
            patchAddress = (u32*)PA_FROM_VA((found + 13));
            found[13] = 0xE1A00000;
        }

        svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, 0x14000000, textTotalSize);
exit:
        svcCloseHandle(processHandle);
    }

    // This function is called before main and before the game starts
    // Useful to do code edits safely
    void    PatchProcess(FwkSettings &settings)
    {
        ToggleTouchscreenForceOn();
        Directory::ChangeWorkingDirectory(TOP_DIR"/resources");
        u64 tid = Process::GetTitleID();
        sprintf(g_ProcessTID, "%016lX", tid);
        LightLock_Init(&regLock);
        LightLock_Init(&openLock);
        OnionSave::initDebug();
        OnionSave::setupPackPaths();
        DEBUG("\n----------------------------------------\nInitializing hooks:\n\n");
        initOnionFSHooks(Process::GetTextSize());
        DEBUG("\nAll hooks initialized, starting game.\n----------------------------------------\n\n");
        Language::Initialize();
    }

    // This function is called when the process exits
    // Useful to save settings, undo patchs or clean up things
    void    OnProcessExit(void)
    {
        ToggleTouchscreenForceOn();
    }
    
    void dummyEntry(MenuEntry *entry){}
    
    void    InitMenu(PluginMenu &menu)
    {
        menu += new MenuEntry("[Coming soon] Custom Character Manager", nullptr, dummyEntry, "");
        menu += new MenuEntry("[Coming soon] Plugin Details", nullptr, dummyEntry, "");
        // Create your entries here, or elsewhere
        // You can create your entries whenever/wherever you feel like it
        
        // Example entry
        /*menu += new MenuEntry("Test", nullptr, [](MenuEntry *entry)
        {
            std::string body("What's the answer ?\n");

            body += std::to_string(42);

            MessageBox("UA", body)();
        });*/
    }

    const std::string about = u8"\n" \
        u8"Mario Kart Wii 3DS - " STRING_VERSION "\n\n"
        u8"Currently purposed for OnionFS\n\n"
        u8"Credits: PabloMK7, Nanquitas, Midou360, NGT,\n"
        u8"CyberYoshi64";

    int     main(void)
    {
        PluginMenu      *menu = new PluginMenu("MKW3DS", MAJOR_VERSION, MINOR_VERSION, REVISION_VERSION, about);

        menu->SynchronizeWithFrame(true);
        menu->ShowWelcomeMessage(false);
        menu->SetHexEditorState(false);
        InitMenu(*menu);
        menu->Run();
        delete menu;
        return 0;
    }
}
