// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <DbgHelp.h>
#include <Shlwapi.h>
#include <direct.h>
#include "IniFile.hpp"
#include "MediaFns.hpp"
#include "FileSystem.h"
#include "SA2ModLoader.h"
#include "ModelInfo.h"
#include "CodeParser.hpp"
#include "Events.h"
#include "patches.h"
#include "testspawn.h"
#include "EXEData.h"
#include "DLLData.h"
#include "DebugText.h"
#include "CrashDump.h"
#include "window.h"
#include "direct3d.h"
#include "Interpolation.h"
#include "TextureReplacement.h"
#include <shlobj.h>
#include <TextConv.hpp>

using namespace std;

const string resourcedir = "resource\\gd_pc\\";
static wstring borderimg = L"mods\\Border.png";

unordered_map<string, unordered_set<string>*> csbfilemap;
struct itercont { unordered_set<string>::const_iterator cur; unordered_set<string>::const_iterator end; };
__out HANDLE WINAPI FindFirstCSBFileA(__in  LPCSTR lpFileName, __out LPWIN32_FIND_DATAA lpFindFileData)
{
	string path = lpFileName;
	path.erase(path.size() - 2);
	path = FileMap::normalizePath(path);
	auto iter = csbfilemap.find(path);
	if (iter == csbfilemap.cend())
		return INVALID_HANDLE_VALUE;
	auto it2 = iter->second->cbegin();
	HANDLE hfind = FindFirstFileA(sadx_fileMap.replaceFile(it2->c_str()), lpFindFileData);
	if (hfind == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;
	FindClose(hfind);
	return new itercont({ it2, iter->second->cend() });
}

BOOL WINAPI FindNextCSBFileA(__in  HANDLE hFindFile, __out LPWIN32_FIND_DATAA lpFindFileData)
{
	itercont *iter = (itercont *)hFindFile;
	++iter->cur;
	if (iter->cur == iter->end)
		return FALSE;
	HANDLE hfind = FindFirstFileA(sadx_fileMap.replaceFile(iter->cur->c_str()), lpFindFileData);
	if (hfind == INVALID_HANDLE_VALUE)
		return FALSE;
	FindClose(hfind);
	return TRUE;
}

BOOL WINAPI FindCSBClose(__inout HANDLE hFindFile)
{
	delete hFindFile;
	return TRUE;
}

unordered_map<ModelIndex *, list<ModelInfo>> modelfiles;

void markobjswapped(NJS_OBJECT *obj)
{
	while (obj)
	{
		IsByteswapped(&obj->evalflags);
		IsByteswapped(&obj->model);
		IsByteswapped(&obj->pos[0]);
		IsByteswapped(&obj->pos[1]);
		IsByteswapped(&obj->pos[2]);
		IsByteswapped(&obj->ang[0]);
		IsByteswapped(&obj->ang[1]);
		IsByteswapped(&obj->ang[2]);
		IsByteswapped(&obj->scl[0]);
		IsByteswapped(&obj->scl[1]);
		IsByteswapped(&obj->scl[2]);
		IsByteswapped(&obj->child);
		IsByteswapped(&obj->sibling);
		if (obj->chunkmodel)
		{
			IsByteswapped(&obj->chunkmodel->vlist);
			IsByteswapped(&obj->chunkmodel->plist);
			IsByteswapped(&obj->chunkmodel->center.x);
			IsByteswapped(&obj->chunkmodel->center.y);
			IsByteswapped(&obj->chunkmodel->center.z);
			IsByteswapped(&obj->chunkmodel->r);
		}
		if (obj->child)
			markobjswapped(obj->child);
		obj = obj->sibling;
	}
}

VoidFunc(sub_4297F0, 0x4297F0);
FunctionPointer(void, sub_48FA80, (NJS_OBJECT *, void *), 0x48FA80);
StdcallFunctionPointer(void, sub_419FC0, (void *), 0x419FC0);
FunctionPointer(void, sub_7A5974, (void *), 0x7A5974);
DataPointer(int, dword_1A55800, 0x1A55800);
DataPointer(int, dword_1AF191C, 0x1AF191C);
DataPointer(void *, dword_1AF1918, 0x1AF1918);
ModelIndex *__cdecl LoadMDLFile_ri(const char *filename)
{
	ModelIndex *result;
	char dir[MAX_PATH];
	PathCombineA(dir, resourcedir.c_str(), filename);
	PathRemoveExtensionA(dir);
	char *fn = PathFindFileNameA(dir);
	char combinedpath[MAX_PATH];
	PathCombineA(combinedpath, dir, fn);
	PathAddExtensionA(combinedpath, ".ini");
	const char *repfn = sadx_fileMap.replaceFile(combinedpath);
	if (PathFileExistsA(repfn))
	{
		FILE *f_mod_ini = fopen(repfn, "r");
		unique_ptr<IniFile> ini(new IniFile(f_mod_ini));
		fclose(f_mod_ini);
		const IniGroup *indexes = ini->getGroup("");
		strncpy_s(dir, repfn, MAX_PATH);
		PathRemoveFileSpecA(dir);
		WIN32_FIND_DATAA data;
		HANDLE hfind = FindFirstFileA((string(dir) + "\\*.sa2mdl").c_str(), &data);
		if (hfind == INVALID_HANDLE_VALUE)
			goto defaultmodelload;
		list<ModelInfo> files;
		vector<ModelIndex> modelindexes;
		sub_4297F0();
		do
		{
			PathCombineA(combinedpath, dir, data.cFileName);
			ModelInfo modelfile(combinedpath);
			files.push_back(modelfile);
			markobjswapped(modelfile.getmodel());
			for (auto i = indexes->cbegin(); i != indexes->cend(); i++)
			{
				void *found = modelfile.getdata(i->second);
				if (found != nullptr)
				{
					int ind = stoi(i->first);
					ModelIndex index = { ind, (NJS_OBJECT *)found };
					if (ind >= 0 && ind < 532 && !CharacterModels[ind].Model)
						CharacterModels[ind] = index;
					modelindexes.push_back(index);
				}
			}
		} while (FindNextFileA(hfind, &data) != 0);
		FindClose(hfind);
		ModelIndex endmarker = { -1, (NJS_OBJECT *)-1 };
		modelindexes.push_back(endmarker);
		result = new ModelIndex[modelindexes.size()];
		memcpy(result, modelindexes.data(), sizeof(ModelIndex) * modelindexes.size());
		modelfiles[result] = files;
		goto end;
	}

defaultmodelload:
	int v3; // edx@3
	int v5; // edi@5
	unsigned int v6; // eax@5
	unsigned int v7; // edi@5
	int v8; // edi@6
	ModelIndex *v9; // eax@7
	signed int v10; // edx@8
	NJS_OBJECT *v11; // ecx@11
	NJS_OBJECT *v12; // ecx@15
	void *v13; // eax@21
	int v14; // ebx@21
	int v15; // edi@21

	result = (ModelIndex *)LoadPRSFile(filename);
	if (result)
	{
		sub_4297F0();
		v3 = 0;
		if (result->Index != -1)
		{
			ModelIndex *v4 = result;
			do
			{
				v5 = v4->Index;
				v6 = (unsigned int)v4->Model;
				v4->Index = (((v4->Index << 16) | v4->Index & 0xFF00) << 8) | ((((unsigned int)v4->Index >> 16) | v5 & 0xFF0000) >> 8);
				v7 = v6;
				++v3;
				v4->Model = (NJS_OBJECT *)((((v6 << 16) | (unsigned __int16)(v6 & 0xFF00)) << 8) | (((v6 >> 16) | v7 & 0xFF0000) >> 8));
				v4 = &result[v3];
			} while (result[v3].Index != -1);
		}
		v8 = 0;
		if (result->Index != -1)
		{
			v9 = result;
			do
			{
				v10 = v9->Index;
				if (v9->Index >= 0 && v10 < 532 && !CharacterModels[v10].Model)
				{
					v11 = v9->Model;
					if (v11 && (unsigned int)v11 <= (unsigned int)result)
					{
						v11 = (NJS_OBJECT *)((DWORD)result + (DWORD)v11);
						v9->Model = v11;
					}
					CharacterModels[v10].Model = v11;
				}
				v12 = v9->Model;
				if (v12)
				{
					if ((unsigned int)v12 <= (unsigned int)result)
					{
						v12 = (NJS_OBJECT *)((DWORD)result + (DWORD)v12);
						v9->Model = v12;
					}
					sub_48FA80(v12, result);
				}
				++v8;
				v9 = &result[v8];
			} while (result[v8].Index != -1);
		}
	end:
		--dword_1A55800;
		if (dword_1AF191C)
		{
			v13 = dword_1AF1918;
			v14 = *((DWORD *)dword_1AF1918 + 1);
			v15 = *((DWORD *)dword_1AF1918 + 1);
			if (!*(BYTE *)(v14 + 21))
			{
				do
				{
					sub_419FC0(*(void **)(v15 + 8));
					v15 = *(DWORD *)v15;
					sub_7A5974((void *)v14);
					v14 = v15;
				} while (!*(BYTE *)(v15 + 21));
				v13 = dword_1AF1918;
			}
			*((DWORD *)v13 + 1) = (DWORD)v13;
			dword_1AF191C = 0;
			*(DWORD *)dword_1AF1918 = (DWORD)dword_1AF1918;
			*((DWORD *)dword_1AF1918 + 2) = (DWORD)dword_1AF1918;
		}
	}
	return result;
}

__declspec(naked) void LoadMDLFile_r()
{
	__asm
	{
		push eax
		call LoadMDLFile_ri
		add esp, 4
		ret
	}
}

void __cdecl ReleaseMDLFile_ri(ModelIndex *a1)
{
	if (a1->Index != -1)
	{
		ModelIndex *v1 = a1;
		do
		{
			if (v1->Index >= 0 && v1->Index < 532 && CharacterModels[v1->Index].Model == v1->Model)
				CharacterModels[v1->Index].Model = 0;
			++v1;
		} while (v1->Index != -1);
	}
	if (modelfiles.find(a1) != modelfiles.cend())
	{
		modelfiles.erase(a1);
		delete[] a1;
	}
	else
	{
		*((DWORD *)a1 - 1) = 0x89ABCDEFu;
		MemoryManager->Deallocate((AllocatedMem *)a1 - 4, "..\\..\\src\\file_ctl.c", 1091);
	}
}

__declspec(naked) void ReleaseMDLFile_r()
{
	__asm
	{
		push esi
		call ReleaseMDLFile_ri
		add esp, 4
		ret
	}
}

void HookImport(const HMODULE hModule, LPCSTR moduleName, const PROC pActualFunction, const PROC pNewFunction)
{
	ULONG ulSize = 0;
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(
		hModule, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulSize);

	if (NULL != pImportDesc)
	{
		for (; pImportDesc->Name; pImportDesc++)
		{
			// get the module name
			PSTR pszModName = (PSTR)((PBYTE)hModule + pImportDesc->Name);

			if (NULL != pszModName)
			{
				// check if the module is kernel32.dll
				if (lstrcmpiA(pszModName, moduleName) == 0)
				{
					// get the module
					PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE)hModule + pImportDesc->FirstThunk);

					for (; pThunk->u1.Function; pThunk++)
					{
						PROC* ppfn = (PROC*)&pThunk->u1.Function;
						if (*ppfn == pActualFunction)
						{
							DWORD dwOldProtect = 0;
							VirtualProtect(ppfn, sizeof(pNewFunction), PAGE_WRITECOPY, &dwOldProtect);
							WriteData(ppfn, pNewFunction);
							VirtualProtect(ppfn, sizeof(pNewFunction), dwOldProtect, &dwOldProtect);
						} // Function that we are looking for
					}
				} // Compare module name
			} // Valid module name
		}
	}
}

void HookTheAPI()
{
	HookImport(GetModuleHandle(NULL), "Kernel32.dll", GetProcAddress(GetModuleHandle(L"Kernel32.dll"), "CreateFileA"), (PROC)MyCreateFileA);
}

char *ShiftJISToUTF8(char *shiftjis)
{
	int cchWcs = MultiByteToWideChar(932, 0, shiftjis, -1, NULL, 0);
	if (cchWcs <= 0) return nullptr;
	wchar_t *wcs = new wchar_t[cchWcs];
	MultiByteToWideChar(932, 0, shiftjis, -1, wcs, cchWcs);
	int cbMbs = WideCharToMultiByte(CP_UTF8, 0, wcs, -1, NULL, 0, NULL, NULL);
	if (cbMbs <= 0) { delete[] wcs; return nullptr; }
	char *utf8 = new char[cbMbs];
	WideCharToMultiByte(CP_UTF8, 0, wcs, -1, utf8, cbMbs, NULL, NULL);
	delete[] wcs;
	return utf8;
}

//used to get external lib location and extra config
std::wstring appPath;
std::wstring extLibPath;

void SetAppPathConfig(std::wstring gamepath)
{
	appPath = gamepath + L"\\SAManager\\"; // Account for portable
	extLibPath = appPath + L"extlib\\";
	WCHAR appDataLocalPath[MAX_PATH];
	if (!Exists(appPath))
	{
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataLocalPath)))
		{
			appPath = appDataLocalPath;
			appPath += L"\\SAManager\\";
			extLibPath = appPath + L"extlib\\";
		}
	}
}

bool dbgConsole, dbgFile, dbgScreen;
ofstream dbgstr;
uint32_t saveedx;
int __cdecl SA2DebugOutput(const char *Format, ...)
{
	__asm { mov saveedx, edx }
	va_list ap;
	va_start(ap, Format);
	int length = vsnprintf(NULL, 0, Format, ap) + 1;
	va_end(ap);
	char *buf = new char[length];
	va_start(ap, Format);
	length = vsnprintf(buf, length, Format, ap);
	va_end(ap);
	if (dbgConsole)
		cout << buf << "\n";
	if (dbgScreen)
		debug_text::DisplayGameDebug(buf);
	if (dbgFile && dbgstr.good())
	{
		char *utf8 = ShiftJISToUTF8(buf);
		dbgstr << utf8 << "\n";
		delete[] utf8;
	}
	delete[] buf;
	__asm { mov edx, saveedx }
	return length;
}

void ScanCSBFolder(string path, int length)
{
	_WIN32_FIND_DATAA data;
	HANDLE hfind = FindFirstFileA((path + "\\*").c_str(), &data);
	if (hfind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (data.cFileName[0] == '.')
			continue;
		else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			string newpath = path + "\\" + data.cFileName;
			_WIN32_FIND_DATAA newdata;
			HANDLE newhfind = FindFirstFileA((newpath + "\\*.csb").c_str(), &newdata);
			if (newhfind != INVALID_HANDLE_VALUE)
			{
				if (length != 0)
				{
					newpath = newpath.substr(length);
					newpath = resourcedir + newpath;
				}
				transform(newpath.begin(), newpath.end(), newpath.begin(), ::tolower);
				unordered_set<string> *files;
				if (csbfilemap.find(newpath) == csbfilemap.cend())
					csbfilemap[newpath] = files = new unordered_set<string>();
				else
					files = csbfilemap[newpath];
				do
				{
					string filebase = newpath + "\\" + newdata.cFileName;
					transform(filebase.begin(), filebase.end(), filebase.begin(), ::tolower);
					files->insert(filebase);
				} while (FindNextFileA(newhfind, &newdata) != 0);
				FindClose(newhfind);
			}
		}
	} while (FindNextFileA(hfind, &data) != 0);
	FindClose(hfind);
}

unordered_map<unsigned char, unordered_map<short, StartPosition>> StartPositions;
bool StartPositionsModified;

struct startposdata { unsigned char character; const StartPosition *positions; };

const startposdata startposaddrs[] = {
	{ Characters_Sonic, SonicStartArray },
	{ Characters_Shadow, ShadowStartArray },
	{ Characters_Tails, TailsStartArray },
	{ Characters_Eggman, TailsStartArray },
	{ Characters_Knuckles, KnucklesStartArray },
	{ Characters_Rouge, RougeStartArray },
	{ Characters_MechTails, MechTailsStartArray },
	{ Characters_MechEggman, MechEggmanStartArray },
	{ Characters_SuperSonic, SuperSonicStartArray },
	{ Characters_SuperShadow, SuperShadowStartArray }
};

void InitializeStartPositionLists()
{
	for (unsigned int i = 0; i < LengthOfArray(startposaddrs); i++)
	{
		StartPositions[startposaddrs[i].character] = unordered_map<short, StartPosition>();
		unordered_map<short, StartPosition> &newlist = StartPositions[startposaddrs[i].character];
		for (const StartPosition* origlist = startposaddrs[i].positions; origlist->Level != LevelIDs_Invalid; ++origlist)
			newlist[origlist->Level] = *origlist;
	}
}

int __cdecl LoadStartPosition_ri(int playerNum, NJS_VECTOR *position, Rotation *rotation)
{
	ObjectMaster *v1 = MainCharacter[playerNum];
	if (position)
	{
		position->z = 0.0;
		position->y = 0.0;
		position->x = 0.0;
	}
	if (rotation)
	{
		rotation->z = 0;
		rotation->y = 0;
		rotation->x = 0;
	}
	if (v1)
	{
		CharObj2Base *v4 = MainCharObj2[playerNum];
		StartPosition *v5;
		if (v4)
		{
			auto iter = StartPositions.find(v4->CharID);
			if (iter == StartPositions.cend())
				return 1;
			auto iter2 = iter->second.find(CurrentLevel);
			if (iter2 == iter->second.cend())
				return 1;
			v5 = &iter2->second;
		}
		else
			return 1;
		int v6;
		if (TwoPlayerMode
			|| (short)CurrentLevel == LevelIDs_SonicVsShadow1
			|| (short)CurrentLevel == LevelIDs_SonicVsShadow2
			|| (short)CurrentLevel == LevelIDs_TailsVsEggman1
			|| (short)CurrentLevel == LevelIDs_TailsVsEggman2
			|| (short)CurrentLevel == LevelIDs_KnucklesVsRouge)
			v6 = (playerNum != 0) + 1;
		else
			v6 = 0;
		if (rotation)
			rotation->y = *(&v5->Rotation1P + v6);
		if (position)
		{
			NJS_VECTOR *v8 = &(&v5->Position1P)[v6];
			position->x = v8->x;
			position->y = v8->y;
			position->z = v8->z;
		}
		return 1;
	}
	return 0;
}

static void __declspec(naked) LoadStartPosition_r()
{
	__asm
	{
		push[esp + 04h] // rotation
		push edi // position
		push ecx // playerNumber

		// Call your __cdecl function here:
		call LoadStartPosition_ri

		pop ecx // playerNumber
		pop edi // position
		add esp, 4 // rotation
		retn
	}
}

unordered_map<unsigned char, unordered_map<short, LevelEndPosition>> _2PIntroPositions;
bool _2PIntroPositionsModified;

struct endposdata { unsigned char character; const LevelEndPosition *positions; };

const endposdata _2pintroposaddrs[] = {
	{ Characters_Sonic, Sonic2PIntroArray },
	{ Characters_Shadow, Shadow2PIntroArray },
	{ Characters_Tails, nullptr },
	{ Characters_Eggman, nullptr },
	{ Characters_Knuckles, Knuckles2PIntroArray },
	{ Characters_Rouge, Rouge2PIntroArray },
	{ Characters_MechTails, MechTails2PIntroArray },
	{ Characters_MechEggman, MechEggman2PIntroArray },
	{ Characters_SuperSonic, nullptr },
	{ Characters_SuperShadow, nullptr }
};

void Initialize2PIntroPositionLists()
{
	for (unsigned int i = 0; i < LengthOfArray(_2pintroposaddrs); i++)
	{
		const LevelEndPosition * origlist = _2pintroposaddrs[i].positions;
		_2PIntroPositions[_2pintroposaddrs[i].character] = unordered_map<short, LevelEndPosition>();
		if (origlist == nullptr)
			continue;
		unordered_map<short, LevelEndPosition> &newlist = _2PIntroPositions[_2pintroposaddrs[i].character];
		for (; origlist->Level != LevelIDs_Invalid; ++origlist)
			newlist[origlist->Level] = *origlist;
	}
}

VoidFunc(sub_434CD0, 0x434CD0);
DataArray(char, byte_1DE4664, 0x1DE4664, 2);
DataPointer(void *, off_1DE95E0, 0x1DE95E0);

// signed int __usercall@<eax>(int a1@<eax>, NJS_VECTOR *a2@<ecx>, char a3)
static const void *const sub_46DC70Ptr = (void*)0x46DC70;
static inline signed int sub_46DC70(int a1, NJS_VECTOR *a2, char a3)
{
	signed int result;
	__asm
	{
		movzx eax, [a3]
		push eax
		mov ecx, [a2]
		mov eax, [a1]
		call sub_46DC70Ptr
		add esp, 4
		mov result, eax
	}
	return result;
}

void __cdecl Load2PIntroPos_ri(int playerNum)
{
	ObjectMaster *v1 = MainCharacter[playerNum];
	EntityData1 *v4;
	NJS_VECTOR *v8;
	if (v1)
	{
		v4 = v1->Data1.Entity;
		CharObj2Base *v3 = MainCharObj2[playerNum];
		if (v3)
		{
			auto iter = _2PIntroPositions.find(v3->CharID);
			if (iter != _2PIntroPositions.cend())
			{
				auto iter2 = iter->second.find(CurrentLevel);
				if (iter2 != iter->second.cend())
				{
					LevelEndPosition *v5 = &iter2->second;
					int v6 = playerNum != 0;
					v4->Rotation.y = *(&v5->Mission2YRotation + v6);
					NJS_VECTOR *v12 = &(&v5->Mission2Position)[v6];
					v4->Position = *v12;
					v8 = &v4->Position;
					*((int *)*(&off_1DE95E0 + playerNum) + 7) = v4->Rotation.y;
					v3->SurfaceInfo.BottomSurfaceDist = v4->Position.y - 10.0f;
					goto LABEL_16;
				}
			}
		}
	}
	v4->Position.z = 0.0;
	v8 = &v4->Position;
	v4->Position.y = 0.0;
	v4->Rotation.y = 0;
	v4->Position.x = 0.0;
LABEL_16:
	sub_46DC70(playerNum, v8, 0);
	v4->Collision->CollisionArray->push |= 0x70u;
	MainCharObj2[playerNum]->CurrentSurfaceFlags = (SurfaceFlags)0;
	byte_1DE4664[playerNum & 1] = *(char*)0x1DE4660;
	CharObj2Base *v9 = MainCharObj2[playerNum];
	float *v10 = (float *)*(&off_1DE95E0 + playerNum);
	if (v9)
	{
		v9->Speed.z = 0.0;
		v9->Speed.y = 0.0;
		v9->Speed.x = 0.0;
	}
	if (v10)
	{
		v10[2] = 0.0;
		v10[1] = 0.0;
		v10[0] = 0.0;
	}
}

__declspec(naked) void Load2PIntroPos_r()
{
	__asm
	{
		push eax
		call Load2PIntroPos_ri
		add esp, 4
		retn
	}
}

unordered_map<unsigned char, unordered_map<short, StartPosition>> EndPositions;
bool EndPositionsModified;

const startposdata endposaddrs[] = {
	{ Characters_Sonic, SonicEndArray },
	{ Characters_Shadow, ShadowEndArray },
	{ Characters_Tails, nullptr },
	{ Characters_Eggman, nullptr },
	{ Characters_Knuckles, KnucklesEndArray },
	{ Characters_Rouge, RougeEndArray },
	{ Characters_MechTails, MechTailsEndArray },
	{ Characters_MechEggman, MechEggmanEndArray },
	{ Characters_SuperSonic, SuperSonicEndArray },
	{ Characters_SuperShadow, SuperShadowEndArray }
};

void InitializeEndPositionLists()
{
	for (unsigned int i = 0; i < LengthOfArray(endposaddrs); i++)
	{
		const StartPosition * origlist = endposaddrs[i].positions;
		EndPositions[endposaddrs[i].character] = unordered_map<short, StartPosition>();
		if (origlist == nullptr)
			continue;
		unordered_map<short, StartPosition> &newlist = EndPositions[endposaddrs[i].character];
		for (; origlist->Level != LevelIDs_Invalid; ++origlist)
			newlist[origlist->Level] = *origlist;
	}
}

bool Mission23EndPositionsModified;
int __cdecl LoadEndPosition_Mission23_ri(int playerNum);
void __cdecl LoadEndPosition_ri(int playerNum)
{
	int v1; // edi
	ObjectMaster *v2; // esi
	CharObj2Base *v3; // eax
	EntityData1 *v4; // esi
	StartPosition *v5; // eax
	int v6; // edx
	NJS_VECTOR *v7; // ecx
	NJS_VECTOR *v9; // eax
	float v10; // ST14_4

	v1 = playerNum;
	v2 = MainCharacter[playerNum];
	if (v2)
	{
		if (Mission23EndPositionsModified)
		{
			if (LoadEndPosition_Mission23_ri(playerNum))
				return;
		}
		else if (LoadEndPosition_Mission23(playerNum))
			return;
		v3 = MainCharObj2[v1];
		v4 = v2->Data1.Entity;
		if (v3)
		{
			auto iter = EndPositions.find(v3->CharID);
			if (iter != EndPositions.cend())
			{
				auto iter2 = iter->second.find(CurrentLevel);
				if (iter2 != iter->second.cend())
				{
					v5 = &iter2->second;
					if (TwoPlayerMode
						|| CurrentLevel == LevelIDs_SonicVsShadow1
						|| CurrentLevel == LevelIDs_SonicVsShadow2
						|| CurrentLevel == LevelIDs_TailsVsEggman1
						|| CurrentLevel == LevelIDs_TailsVsEggman2
						|| CurrentLevel == LevelIDs_KnucklesVsRouge)
						v6 = (v1 != 0) + 1;
					else
						v6 = 0;
					v4->Rotation.z = 0;
					v4->Rotation.x = 0;
					v4->Rotation.y = *(&v5->Rotation1P + v6);
					*((int *)*(&off_1DE95E0 + playerNum) + 7) = v4->Rotation.y;
					v9 = &v5->Position1P + v6;
					v4->Position.x = v9->x;
					v7 = &v4->Position;
					v7->y = v9->y;
					v7->z = v9->z;
					v10 = v4->Position.y - 10;
					MainCharObj2[v1]->SurfaceInfo.BottomSurfaceDist = v10;
					MainCharObj2[v1]->SomeVectors[0].x = 0;
					goto LABEL_27;
				}
			}
		}
		v4->Rotation.z = 0;
		v4->Rotation.y = 0;
		v4->Rotation.x = 0;
		v4->Position.z = 0.0;
		v7 = &v4->Position;
		v4->Position.y = 0.0;
		v4->Position.x = 0.0;
		*((int *)*(&off_1DE95E0 + playerNum) + 7) = 0;
	LABEL_27:
		sub_46DC70(v1, v7, 0);
		v4->Collision->CollisionArray->push |= 0x70u;
		MainCharObj2[v1]->CurrentSurfaceFlags = (SurfaceFlags)0;
		if (CurrentLevel == LevelIDs_RadicalHighway || CurrentLevel == LevelIDs_LostColony)
		{
			byte_1DE4664[v1 & 1] = 5;
		}
		else
		{
			byte_1DE4664[playerNum & 1] = *(char*)0x1DE4660;
		}
	}
}

__declspec(naked) void LoadEndPosition_r()
{
	__asm
	{
		push eax
		call LoadEndPosition_ri
		add esp, 4
		retn
	}
}

unordered_map<unsigned char, unordered_map<short, LevelEndPosition>> Mission23EndPositions;

const endposdata m23endposaddrs[] = {
	{ Characters_Sonic, SonicMission23EndArray },
	{ Characters_Shadow, ShadowMission23EndArray },
	{ Characters_Tails, nullptr },
	{ Characters_Eggman, nullptr },
	{ Characters_Knuckles, KnucklesMission23EndArray },
	{ Characters_Rouge, RougeMission23EndArray },
	{ Characters_MechTails, MechTailsMission23EndArray },
	{ Characters_MechEggman, MechEggmanMission23EndArray },
	{ Characters_SuperSonic, nullptr },
	{ Characters_SuperShadow, nullptr }
};

void InitializeMission23EndPositionLists()
{
	for (unsigned int i = 0; i < LengthOfArray(endposaddrs); i++)
	{
		const LevelEndPosition * origlist = m23endposaddrs[i].positions;
		Mission23EndPositions[m23endposaddrs[i].character] = unordered_map<short, LevelEndPosition>();
		if (origlist == nullptr)
			continue;
		unordered_map<short, LevelEndPosition> &newlist = Mission23EndPositions[m23endposaddrs[i].character];
		for (; origlist->Level != LevelIDs_Invalid; ++origlist)
			newlist[origlist->Level] = *origlist;
	}
}

int __cdecl LoadEndPosition_Mission23_ri(int playerNum)
{
	int v1; // edi
	__int16 v2; // bp
	int v3; // edx
	EntityData1 *v4; // esi
	LevelEndPosition *v5; // eax
	int v7; // edi
	NJS_VECTOR *v9; // eax
	NJS_VECTOR *v10; // ecx
	float v11; // ST14_4

	v1 = playerNum;
	if (TwoPlayerMode)
	{
		return 0;
	}
	v2 = CurrentLevel;
	if (CurrentLevel == LevelIDs_SonicVsShadow1
		|| CurrentLevel == LevelIDs_SonicVsShadow2
		|| CurrentLevel == LevelIDs_TailsVsEggman1
		|| CurrentLevel == LevelIDs_TailsVsEggman2
		|| CurrentLevel == LevelIDs_KnucklesVsRouge
		|| CurrentLevel > LevelIDs_BigFoot && CurrentLevel != LevelIDs_Route101280)
	{
		return 0;
	}
	if (MissionNum != 1)
	{
		if (MissionNum == 2)
		{
			v3 = 1;
			goto LABEL_13;
		}
		return 0;
	}
	v3 = 0;
LABEL_13:
	v4 = MainCharacter[playerNum]->Data1.Entity;
	auto iter = Mission23EndPositions.find(GetCharacterID(playerNum));
	if (iter == Mission23EndPositions.cend())
		return 0;
	auto iter2 = iter->second.find(CurrentLevel);
	if (iter2 == iter->second.cend())
		return 0;
	v5 = &iter2->second;
	if (*(&v5->Mission2YRotation + v3) == 0xFFFF)
	{
		return 0;
	}
	v4->Rotation.z = 0;
	v4->Rotation.x = 0;
	v4->Rotation.y = *(&v5->Mission2YRotation + v3);
	*((int *)*(&off_1DE95E0 + playerNum) + 7) = v4->Rotation.y;
	v9 = &v5->Mission2Position + v3;
	v4->Position.x = v9->x;
	v10 = &v4->Position;
	v10->y = v9->y;
	v10->z = v9->z;
	v11 = v4->Position.y - 10;
	MainCharObj2[v1]->SurfaceInfo.BottomSurfaceDist = v11;
	MainCharObj2[v1]->SomeVectors[0].x = 0;
	sub_46DC70(v1, &v4->Position, 0);
	v4->Collision->CollisionArray->push |= 0x70u;
	MainCharObj2[v1]->CurrentSurfaceFlags = (SurfaceFlags)0;
	v7 = v1 & 1;
	if (CurrentLevel == LevelIDs_LostColony)
	{
		byte_1DE4664[v7] = 5;
	}
	else
	{
		byte_1DE4664[v7] = *(char*)0x1DE4660;
	}
	return 1;
}

static void __declspec(naked) LoadEndPosition_Mission23_r()
{
	__asm
	{
		push eax // playerNum

		// Call your __cdecl function here:
		call LoadEndPosition_Mission23_ri

		add esp, 4 // playerNum<eax> is also used for return value
		retn
	}
}

bool isGameLoaded = false;
void sub_434CD0_r() {

	if (NextGameMode == 12 && !isGameLoaded) {
		NextGameMode = 13;
		isGameLoaded = true;
	}

	return sub_434CD0();
}

const char *mainsavepath = "resource/gd_PC/SAVEDATA";
const char *chaosavepath = "resource/gd_PC/SAVEDATA";
extern HelperFunctions helperFunctions;

bool IsPathExist(const string& s);

// Backward compatibility exports
// Remove when it is safe to assume that no mod are using these.
extern "C"
{
	__declspec(dllexport) void __cdecl SetDebugFontSize(float size)
	{
		debug_text::SetFontSize(size);
	}

	__declspec(dllexport) void __cdecl SetDebugFontColor(int color)
	{
		debug_text::SetFontColor(color);
	}

	__declspec(dllexport) void __cdecl DisplayDebugString(int loc, const char* str)
	{
		debug_text::DisplayString(loc, str);
	}

	__declspec(dllexport) void __cdecl PrintDebugNumber(int loc, int value, signed int numdigits)
	{
		debug_text::DisplayNumber(loc, value, numdigits);
	}

	__declspec(dllexport) void __cdecl DisplayDebugStringFormatted(int loc, const char* Format, ...)
	{
		va_list ap;
		va_start(ap, Format);
		int result = vsnprintf(nullptr, 0, Format, ap) + 1;
		va_end(ap);
		char* buf = new char[result + 1];
		va_start(ap, Format);
		result = vsnprintf(buf, result + 1, Format, ap);
		va_end(ap);

		debug_text::DisplayString(loc, buf);
	}

	__declspec(dllexport) void __cdecl sub_759AA0(int a1, int a2, int a3, int a4, int a5)
	{
		debug_text::sub_759AA0(a1, a2, a3, a4, a5);
	}
}

void SyncLoad(void (*a1)(void*), void* a2)
{
	a1(a2);
}

char inilangs[] {
	Language_English,
	Language_German,
	Language_Spanish,
	Language_French,
	Language_Italian,
	Language_Japanese
};

static vector<string>& split(const string& s, char delim, vector<string>& elems)
{
	std::stringstream ss(s);
	string item;

	while (std::getline(ss, item, delim))
	{
		elems.push_back(item);
	}

	return elems;
}


static vector<string> split(const string& s, char delim)
{
	vector<string> elems;
	split(s, delim, elems);
	return elems;
}

LoaderSettings loaderSettings = {};
std::vector<Mod> modlist;
void __cdecl InitMods(void)
{
	**datadllhandle = LoadLibrary(L".\\resource\\gd_PC\\DLL\\Win32\\Data_DLL_orig.dll");
	if (!**datadllhandle)
	{
		MessageBox(NULL, L"Data_DLL_orig.dll could not be loaded!\n\nSA2 will now proceed to abruptly exit.", L"SA2 Mod Loader", MB_ICONERROR);
		ExitProcess(1);
	}
	HookTheAPI();
	FILE *f_ini = _wfopen(L"mods\\SA2ModLoader.ini", L"r");
	if (!f_ini)
	{
		MessageBox(NULL, L"mods\\SA2ModLoader.ini could not be read!", L"SA2 Mod Loader", MB_ICONWARNING);
		return;
	}
	unique_ptr<IniFile> ini(new IniFile(f_ini));
	fclose(f_ini);
	// Get sonic.exe's path and filename.
	wchar_t pathbuf[MAX_PATH];
	GetModuleFileName(nullptr, pathbuf, MAX_PATH);
	wstring exepath(pathbuf);
	wstring exefilename;
	string::size_type slash_pos = exepath.find_last_of(L"/\\");
	if (slash_pos != string::npos)
	{
		exefilename = exepath.substr(slash_pos + 1);
		if (slash_pos > 0)
			exepath = exepath.substr(0, slash_pos);
	}

	// Convert the EXE filename to lowercase.
	transform(exefilename.begin(), exefilename.end(), exefilename.begin(), ::towlower);
	// Get path for Mod Manager settings and libraries
	SetAppPathConfig(exepath);

	const IniGroup *setgrp = ini->getGroup("");

	loaderSettings.DebugConsole = setgrp->getBool("DebugConsole");
	loaderSettings.DebugScreen = setgrp->getBool("DebugScreen");
	loaderSettings.DebugFile = setgrp->getBool("DebugFile");
	loaderSettings.DebugCrashLog = setgrp->getBool("DebugCrashLog", true);
	loaderSettings.PauseWhenInactive = setgrp->getBool("PauseWhenInactive", true);
	loaderSettings.DisableExitPrompt = setgrp->getBool("DisableExitPrompt");
	loaderSettings.ScreenNum = setgrp->getInt("ScreenNum", 1);
	loaderSettings.BorderlessWindow = setgrp->getBool("BorderlessWindow");
	loaderSettings.FullScreen = setgrp->getBool("FullScreen");
	loaderSettings.SkipIntro = setgrp->getBool("SkipIntro");
	loaderSettings.SyncLoad = setgrp->getBool("SyncLoad", true);
	loaderSettings.HorizontalResolution = setgrp->getInt("HorizontalResolution", 640);
	loaderSettings.VerticalResolution = setgrp->getInt("VerticalResolution", 480);
	loaderSettings.VoiceLanguage = setgrp->getInt("VoiceLanguage", 1);
	loaderSettings.TextLanguage = inilangs[setgrp->getInt("TextLanguage", 0)];
	loaderSettings.CustomWindowSize = setgrp->getBool("CustomWindowSize");
	loaderSettings.WindowWidth = setgrp->getInt("WindowWidth", 640);
	loaderSettings.WindowHeight = setgrp->getInt("WindowHeight", 480);
	loaderSettings.ResizableWindow = setgrp->getBool("ResizableWindow");
	loaderSettings.MaintainAspectRatio = setgrp->getBool("MaintainAspectRatio");
	loaderSettings.FramerateLimiter = setgrp->getBool("FramerateLimiter");
	loaderSettings.TestSpawnLevel = setgrp->getInt("TestSpawnLevel");
	loaderSettings.TestSpawnCharacter = setgrp->getInt("TestSpawnCharacter");
	loaderSettings.TestSpawnPositionEnabled = setgrp->getBool("TestSpawnPositionEnabled");
	loaderSettings.TestSpawnX = setgrp->getInt("TestSpawnX");
	loaderSettings.TestSpawnY = setgrp->getInt("TestSpawnY");
	loaderSettings.TestSpawnZ = setgrp->getInt("TestSpawnZ");
	loaderSettings.TestSpawnRotation = setgrp->getInt("TestSpawnRotation");
	loaderSettings.TestSpawnEvent = setgrp->getInt("TestSpawnEvent");
	loaderSettings.TestSpawnSaveID = setgrp->getInt("TestSpawnSaveID");
	loaderSettings.EnableBass = setgrp->getBool("EnableBass", true);

	direct3d::init();

	if (loaderSettings.FramerateLimiter)
	{
		direct3d::enable_frame_limiter();
	}

	if (loaderSettings.SyncLoad)
	{
		WriteJump((void*)0x428470, SyncLoad);
		WriteJump((void*)0x428740, SyncLoad);
	}

	if (loaderSettings.DebugConsole)
	{
		AllocConsole();
		SetConsoleTitle(L"SA2 Mod Loader output");
		freopen("CONOUT$", "wb", stdout);
		dbgConsole = true;
	}
	dbgScreen = loaderSettings.DebugScreen;
	if (loaderSettings.DebugFile)
	{
		dbgstr = ofstream("mods\\SA2ModLoader.log", ios_base::ate | ios_base::app);
		dbgFile = dbgstr.is_open();
	}
	debug_text::Initialize();
	if (dbgConsole || dbgFile || dbgScreen)
	{
		WriteJump(PrintDebug, SA2DebugOutput);
		PrintDebug("SA2 Mod Loader version %d, built %s", ModLoaderVer, __TIMESTAMP__);
	}

	VoiceLanguage = loaderSettings.VoiceLanguage;

	texpack::init();

	// Unprotect the .rdata section.
	// TODO: Get .rdata address and length dynamically.
	DWORD oldprot;
	VirtualProtect((void *)0x87342C, 0xA3BD4, PAGE_WRITECOPY, &oldprot);

	WriteJump((void *)LoadMDLFilePtr, LoadMDLFile_r);
	WriteJump((void *)ReleaseMDLFilePtr, ReleaseMDLFile_r);
	WriteData((char*)0x435A44, (char)0x90u);
	WriteCall((void*)0x435A45, FindFirstCSBFileA);
	WriteData((char*)0x435BD6, (char)0x90u);
	WriteCall((void*)0x435BD7, FindNextCSBFileA);
	WriteData((char*)0x435BE5, (char)0x90u);
	WriteCall((void*)0x435BE6, FindCSBClose);
	WriteData((char*)0x435D4F, (char)0x90u);
	WriteCall((void*)0x435D50, FindFirstCSBFileA);
	WriteData((char*)0x435EE6, (char)0x90u);
	WriteCall((void*)0x435EE7, FindNextCSBFileA);
	WriteData((char*)0x435EF5, (char)0x90u);
	WriteCall((void*)0x435EF6, FindCSBClose);

	sadx_fileMap.scanPRSFolder("resource\\gd_PC");
	sadx_fileMap.scanPRSFolder("resource\\gd_PC\\event");
	
	ApplyPatches();

	// Map of files to replace.
	// This is done with a second map instead of sadx_fileMap directly
	// in order to handle multiple mods.
	unordered_map<string, string> filereplaces;
	vector<std::pair<string, string>> fileswaps;

	InitializeStartPositionLists();
	Initialize2PIntroPositionLists();
	InitializeEndPositionLists();
	InitializeMission23EndPositionLists();

	ScanCSBFolder("resource\\gd_PC\\MLT", 0);
	ScanCSBFolder("resource\\gd_PC\\MPB", 0);
	ScanCSBFolder("resource\\gd_PC\\event\\MLT", 0);

	if (loaderSettings.EnableBass)
		Init_AudioBassHook();

	//init_interpolationAnimFixes(); //disabled for now since it is not fully functional 

	if (loaderSettings.DebugCrashLog)
		initCrashDump();

	vector<std::pair<ModInitFunc, string>> initfuncs;
	vector<std::pair<string, string>> errors;

	string _mainsavepath, _chaosavepath;

	// It's mod loading time!
	PrintDebug("Loading mods...");
	char key[8];
	for (int i = 1; i <= 999; i++)
	{
		sprintf_s(key, "Mod%d", i);
		if (!setgrp->hasKey(key))
			break;
		const string mod_dirA = "mods\\" + setgrp->getString(key);
		const wstring mod_dir = L"mods\\" + setgrp->getWString(key);
		const string mod_inifile = mod_dirA + "\\mod.ini";
		FILE *f_mod_ini = fopen(mod_inifile.c_str(), "r");
		if (!f_mod_ini)
		{
			PrintDebug("Could not open file mod.ini in \"mods\\%s\".\n", mod_dirA.c_str());
			errors.push_back(std::pair<string, string>(mod_dirA, "mod.ini missing"));
			continue;
		}
		unique_ptr<IniFile> ini_mod(new IniFile(f_mod_ini));
		fclose(f_mod_ini);

		const IniGroup *modinfo = ini_mod->getGroup("");
		const string mod_name = modinfo->getString("Name");
		PrintDebug("%d. %s\n", i, mod_name.c_str());

		vector<ModDependency> moddeps;

		for (unsigned int j = 1; j <= 999; j++)
		{
			char key2[14];
			snprintf(key2, sizeof(key2), "Dependency%u", j);
			if (!modinfo->hasKey(key2))
				break;
			auto dep = split(modinfo->getString(key2), '|');
			moddeps.push_back({ _strdup(dep[0].c_str()), _strdup(dep[1].c_str()), _strdup(dep[2].c_str()), _strdup(dep[3].c_str()) });
		}

		ModDependency* deparr = new ModDependency[moddeps.size()];
		memcpy(deparr, moddeps.data(), moddeps.size() * sizeof(ModDependency));

		Mod modinf = {
			_strdup(mod_name.c_str()),
			_strdup(modinfo->getString("Author").c_str()),
			_strdup(modinfo->getString("Description").c_str()),
			_strdup(modinfo->getString("Version").c_str()),
			_strdup(mod_dirA.c_str()),
			_strdup(modinfo->getString("ModID", mod_dirA).c_str()),
			NULL,
			modinfo->getBool("RedirectMainSave"),
			modinfo->getBool("RedirectChaoSave"),
			{
				deparr,
				moddeps.size()
			}
		};

		if (ini_mod->hasGroup("IgnoreFiles"))
		{
			const IniGroup *group = ini_mod->getGroup("IgnoreFiles");
			auto data = group->data();
			for (unordered_map<string, string>::const_iterator iter = data->begin();
				iter != data->end(); ++iter)
			{
				sadx_fileMap.addIgnoreFile(iter->first, i);
				PrintDebug("Ignored file: %s\n", iter->first.c_str());
			}
		}

		if (ini_mod->hasGroup("ReplaceFiles"))
		{
			const IniGroup *group = ini_mod->getGroup("ReplaceFiles");
			auto data = group->data();
			for (unordered_map<string, string>::const_iterator iter = data->begin();
				iter != data->end(); ++iter)
			{
				filereplaces[FileMap::normalizePath(iter->first)] =
					FileMap::normalizePath(iter->second);
			}
		}

		if (ini_mod->hasGroup("SwapFiles"))
		{
			const IniGroup* group = ini_mod->getGroup("SwapFiles");
			auto data = group->data();
			for (const auto& iter : *data)
			{
				fileswaps.emplace_back(FileMap::normalizePath(iter.first),
					FileMap::normalizePath(iter.second));
			}
		}

		// Check for gd_pc replacements
		string sysfol = mod_dirA + "\\gd_pc";
		transform(sysfol.begin(), sysfol.end(), sysfol.begin(), ::tolower);
		if (DirectoryExists(sysfol))
		{
			sadx_fileMap.scanFolder(sysfol, i);
			ScanCSBFolder(sysfol + "\\mlt", sysfol.length() + 1);
			ScanCSBFolder(sysfol + "\\mpb", sysfol.length() + 1);
			ScanCSBFolder(sysfol + "\\event\\mlt", sysfol.length() + 1);
		}

		const string modTexDir = mod_dirA + "\\textures";
		if (DirectoryExists(modTexDir))
			sadx_fileMap.scanTextureFolder(modTexDir, i);

		const string modRepTexDir = mod_dirA + "\\replacetex";
		if (DirectoryExists(modRepTexDir))
			ScanTextureReplaceFolder(modRepTexDir, i);

		// Check if a custom EXE is required.
		if (modinfo->hasKeyNonEmpty("EXEFile"))
		{
			wstring modexe = modinfo->getWString("EXEFile");
			transform(modexe.begin(), modexe.end(), modexe.begin(), ::towlower);

			// Are we using the correct EXE?
			if (modexe != exefilename)
			{
				wchar_t msg[4096];
				swprintf(msg, LengthOfArray(msg),
					L"Mod \"%s\" should be run from \"%s\", but you are running \"%s\".\n\n"
					L"Continue anyway?", mod_name.c_str(), modexe.c_str(), exefilename.c_str());
				if (MessageBox(nullptr, msg, L"SA2 Mod Loader", MB_ICONWARNING | MB_YESNO) == IDNO)
					ExitProcess(1);
			}
		}

		// Check if the mod has a DLL file.
		if (modinfo->hasKeyNonEmpty("DLLFile"))
		{
			// Prepend the mod directory.
			wstring dll_filename = mod_dir + L'\\' + modinfo->getWString("DLLFile");
			HMODULE module = LoadLibrary(dll_filename.c_str());

			if (module == nullptr)
			{
				DWORD error = GetLastError();
				LPSTR buffer;
				size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL);

				bool allocated = (size != 0);

				if (!allocated)
				{
					static const char fmterr[] = "Unable to format error message.";
					buffer = const_cast<LPSTR>(fmterr);
					size = LengthOfArray(fmterr) - 1;
				}

				string message(buffer, size);
				LocalFree(buffer);

				PrintDebug("Failed loading mod DLL \"%s\": %s\n", dll_filename.c_str(), message.c_str());
				errors.push_back(std::pair<string, string>(mod_name, "DLL error - " + message));
			}
			else
			{
				const ModInfo *info = (ModInfo *)GetProcAddress(module, "SA2ModInfo");
				if (info)
				{
					modinf.DLLHandle = module;
					if (info->Patches)
					{
						for (int j = 0; j < info->PatchCount; j++)
							WriteData(info->Patches[j].address, info->Patches[j].data, info->Patches[j].datasize);
					}
					if (info->Jumps)
					{
						for (int j = 0; j < info->JumpCount; j++)
							WriteJump(info->Jumps[j].address, info->Jumps[j].data);
					}
					if (info->Calls)
					{
						for (int j = 0; j < info->CallCount; j++)
							WriteCall(info->Calls[j].address, info->Calls[j].data);
					}
					if (info->Pointers)
					{
						for (int j = 0; j < info->PointerCount; j++)
							WriteData((void**)info->Pointers[j].address, info->Pointers[j].data);
					}
					if (info->Init)
					{
						initfuncs.push_back({ info->Init, mod_dirA });
					}

					const ModInitFunc init = (const ModInitFunc)GetProcAddress(module, "Init");
					if (init)
						initfuncs.push_back({ init, mod_dirA });

					const PatchList* patches = (const PatchList*)GetProcAddress(module, "Patches");
					if (patches)
					{
						for (int j = 0; j < patches->Count; j++)
							WriteData(patches->Patches[j].address, patches->Patches[j].data, patches->Patches[j].datasize);
					}

					const PointerList* jumps = (const PointerList*)GetProcAddress(module, "Jumps");
					if (jumps)
					{
						for (int j = 0; j < jumps->Count; j++)
							WriteJump(jumps->Pointers[j].address, jumps->Pointers[j].data);
					}

					const PointerList* calls = (const PointerList*)GetProcAddress(module, "Calls");
					if (calls)
					{
						for (int j = 0; j < calls->Count; j++)
							WriteCall(calls->Pointers[j].address, calls->Pointers[j].data);
					}

					const PointerList* pointers = (const PointerList*)GetProcAddress(module, "Pointers");
					if (pointers)
					{
						for (int j = 0; j < pointers->Count; j++)
							WriteData((void**)pointers->Pointers[j].address, pointers->Pointers[j].data);
					}

					RegisterEvent(modFrameEvents, module, "OnFrame");
					RegisterEvent(modInputEvents, module, "OnInput");
					RegisterEvent(modControlEvents, module, "OnControl");
					RegisterEvent(modExitEvents, module, "OnExit");
					RegisterEvent(modRenderDeviceLost, module, "OnRenderDeviceLost");
					RegisterEvent(modRenderDeviceReset, module, "OnRenderDeviceReset");
					RegisterEvent(onRenderSceneEnd, module, "OnRenderSceneEnd");
					RegisterEvent(onRenderSceneStart, module, "OnRenderSceneStart");
				}
				else
				{
					PrintDebug("File \"%s\" is not a valid mod file.\n", dll_filename.c_str());
					errors.push_back(std::pair<string, string>(mod_name, "Not a valid mod file."));
				}
			}
		}

		// Check if the mod has EXE data replacements.
		if (modinfo->hasKeyNonEmpty("EXEData"))
		{
			wchar_t filename[MAX_PATH];
			swprintf(filename, LengthOfArray(filename), L"%s\\%s",
				mod_dir.c_str(), modinfo->getWString("EXEData").c_str());
			ProcessEXEData(filename, mod_dir);
		}

		// Check if the mod has DLL data replacements.
		if (modinfo->hasKeyNonEmpty("DLLData"))
		{
			wchar_t filename[MAX_PATH];
			swprintf(filename, LengthOfArray(filename), L"%s\\%s",
				mod_dir.c_str(), modinfo->getWString("DLLData").c_str());
			ProcessDLLData(filename, mod_dir);
		}

		if (modinfo->getBool("RedirectMainSave")) {
			_mainsavepath = mod_dirA + "\\SAVEDATA";

			if (!IsPathExist(_mainsavepath))
			{
				_mkdir(_mainsavepath.c_str());
			}
		}

		if (modinfo->getBool("RedirectChaoSave")) {
			_chaosavepath = mod_dirA + "\\SAVEDATA";

			if (!IsPathExist(_chaosavepath))
			{
				_mkdir(_chaosavepath.c_str());
			}
		}

		if (modinfo->hasKeyNonEmpty("BorderImage"))
			borderimg = mod_dir + L'\\' + modinfo->getWString("BorderImage");

		modlist.push_back(modinf);
	}

	PatchWindow(loaderSettings, borderimg);

	if (!errors.empty())
	{
		std::stringstream message;
		message << "The following mods didn't load correctly:" << std::endl;

		for (auto& i : errors)
			message << std::endl << i.first << ": " << i.second;

		MessageBoxA(nullptr, message.str().c_str(), "Mods failed to load", MB_OK | MB_ICONERROR);
	}

	// Replace filenames. ("ReplaceFiles")
	for (const auto& filereplace : filereplaces)
	{
		sadx_fileMap.addReplaceFile(filereplace.first, filereplace.second);
	}
	for (const auto& fileswap : fileswaps)
	{
		sadx_fileMap.swapFiles(fileswap.first, fileswap.second);
	}

	for (unsigned int i = 0; i < initfuncs.size(); i++)
		initfuncs[i].first(initfuncs[i].second.c_str(), helperFunctions);

	TestSpawnCheckArgs(helperFunctions);

	if (StartPositionsModified)
		WriteJump((void *)LoadStartPositionPtr, LoadStartPosition_r);
	if (_2PIntroPositionsModified)
		WriteJump((void *)Load2PIntroPosPtr, Load2PIntroPos_r);
	if (EndPositionsModified)
		WriteJump((void *)LoadEndPositionPtr, LoadEndPosition_r);
	if (Mission23EndPositionsModified)
		WriteJump((void *)LoadEndPosition_Mission23Ptr, LoadEndPosition_Mission23_r);

	if (!_mainsavepath.empty())
	{
		char *buf = new char[_mainsavepath.size() + 1];
		strncpy(buf, _mainsavepath.c_str(), _mainsavepath.size() + 1);
		mainsavepath = buf;
		string tmp = "./" + _mainsavepath + "/SONIC2B__S%02d";
		buf = new char[tmp.size() + 1];
		strncpy(buf, tmp.c_str(), tmp.size() + 1);
		WriteData((char **)0x445312, buf);
		WriteData((char **)0x689684, buf);
		WriteData((char **)0x689AA9, buf);
		WriteData((char **)0x689D22, buf);
		WriteData((char **)0x689D4D, buf);
		tmp = "./" + _mainsavepath + "/SONIC2B__D%02d";
		buf = new char[tmp.size() + 1];
		strncpy(buf, tmp.c_str(), tmp.size() + 1);
		WriteData((char **)0x445332, buf);
		tmp = "./" + _mainsavepath + "/SONIC2B__S01";
		buf = new char[tmp.size() + 1];
		strncpy(buf, tmp.c_str(), tmp.size() + 1);
		WriteData((char **)0x445317, buf);
		WriteData((char **)0x689689, buf);
		WriteData((char **)0x689AAE, buf);
		WriteData((char **)0x689D27, buf);
		WriteData((char **)0x689D52, buf);
		WriteData((char **)0x173D070, buf);
		tmp = "./" + _mainsavepath + "/SONIC2B__D01";
		buf = new char[tmp.size() + 1];
		strncpy(buf, tmp.c_str(), tmp.size() + 1);
		WriteData((char **)0x445337, buf);
		WriteData((char **)0x173D07C, buf);
	}

	if (!_chaosavepath.empty())
	{
		char *buf = new char[_chaosavepath.size() + 1];
		strncpy(buf, _chaosavepath.c_str(), _chaosavepath.size() + 1);
		chaosavepath = buf;
		string tmp = "./" + _chaosavepath + "/SONIC2B__ALF";
		buf = new char[tmp.size() + 1];
		strncpy(buf, tmp.c_str(), tmp.size() + 1);
		WriteData((char **)0x457027, buf);
		WriteData((char **)0x52DE84, buf);
		WriteData((char **)0x52DF48, buf);
		WriteData((char **)0x52E063, buf);
		WriteData((char **)0x52E2A8, buf);
		WriteData((char **)0x52FEC7, buf);
		WriteData((char **)0x5323A1, buf);
		WriteData((char **)0x5323B5, buf);
		WriteData((char **)0x5324A2, buf);
		WriteData((char **)0x53257E, buf);
		WriteData((char **)0x532595, buf);
		WriteData((char **)0x532672, buf);
	}

	PrintDebug("Mod loading finished.");

	CheckCrashMod();

	ifstream patches_str("mods\\Patches.dat", ifstream::binary);
	if (patches_str.is_open())
	{
		CodeParser patchParser;
		static const char codemagic[6] = { 'c', 'o', 'd', 'e', 'v', '5' };
		char buf[sizeof(codemagic)];
		patches_str.read(buf, sizeof(buf));
		if (!memcmp(buf, codemagic, sizeof(codemagic)))
		{
			int codecount_header;
			patches_str.read((char*)&codecount_header, sizeof(codecount_header));
			PrintDebug("Loading %d patches...\n", codecount_header);
			patches_str.seekg(0);
			int codecount = patchParser.readCodes(patches_str);
			if (codecount >= 0)
			{
				PrintDebug("Loaded %d patches.\n", codecount);
				patchParser.processCodeList();
			}
			else
			{
				PrintDebug("ERROR loading patches: ");
				switch (codecount)
				{
				case -EINVAL:
					PrintDebug("Patch file is not in the correct format.\n");
					break;
				default:
					PrintDebug("%s\n", strerror(-codecount));
					break;
				}
			}
		}
		else
		{
			PrintDebug("Patch file is not in the correct format.\n");
		}
		patches_str.close();
	}

	ifstream codes_str("mods\\Codes.dat", ifstream::binary);
	if (codes_str.is_open())
	{
		static const char codemagic[6] = { 'c', 'o', 'd', 'e', 'v', '5' };
		char buf[sizeof(codemagic)];
		codes_str.read(buf, sizeof(buf));
		if (!memcmp(buf, codemagic, sizeof(codemagic)))
		{
			int codecount_header;
			codes_str.read((char*)&codecount_header, sizeof(codecount_header));
			PrintDebug("Loading %d codes...\n", codecount_header);
			codes_str.seekg(0);
			int codecount = codeParser.readCodes(codes_str);
			if (codecount >= 0)
			{
				PrintDebug("Loaded %d codes.\n", codecount);
				codeParser.processCodeList();
			}
			else
			{
				PrintDebug("ERROR loading codes: ");
				switch (codecount)
				{
				case -EINVAL:
					PrintDebug("Code file is not in the correct format.\n");
					break;
				default:
					PrintDebug("%s\n", strerror(-codecount));
					break;
				}
			}
		}
		else
		{
			PrintDebug("Code file is not in the correct format.\n");
		}
		codes_str.close();
	}

	if (loaderSettings.SkipIntro || testSpawnCutscene)
	{
		if (!testSpawnLvl)
			WriteData(reinterpret_cast<int*>(0x43459A), static_cast<int>(GameMode_LoadAdvertise)); //change gamemode	

		if (!testSpawnCutscene)
			WriteCall((void*)0x434778, sub_434CD0_r);

		if (*(int*)0x428010 != 0xC3) //if the code to disable loading hint is disabled
		{
			LoadTipsTexs(TextLanguage); // Skipped. Loaded during copyright screen.
		}
	}

	// Sets up code/event handling
	InitOnFrame();	// OnFrame
	WriteJump((void*)0x0077E897, OnInput);
	WriteJump((void*)0x00441D41, OnControl);
	WriteJump((void*)0x00441EEB, OnControl);
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	int bufsize;
	char* buf;
	string sa2dir;
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		bufsize = GetCurrentDirectoryA(0, NULL);
		buf = new char[bufsize];
		GetCurrentDirectoryA(bufsize, buf);
		sa2dir = buf;
		delete[] buf;
		transform(sa2dir.begin(), sa2dir.end(), sa2dir.begin(), ::tolower);
		sa2dir += "\\";
		sadx_fileMap.setSA2Dir(sa2dir);
		WriteJump((void *)0x0077DD5C, InitMods);
		WriteJump((void *)0x0077DD43, InitMods);
		break;
	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
