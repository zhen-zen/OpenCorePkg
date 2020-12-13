/** @file
 Control CFGLock BIOS Option
 
 Copyright (c) 2020, Brumbaer. All rights reserved.<BR>
 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution.  The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php
 
 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 
 **/
#include "ControlMsrE2.h"

#define CONTEXTS_MAX 8

EFI_STATUS WalkListHeaders (
                            IN EFI_HII_HANDLE * handles,
                            IN EFI_HII_PACKAGE_LIST_HEADER **ListHeaders,
                            IN UINT32 ListHeaderCount,
                            IN EFI_STRING SearchString
                            ) {
    UINT16           OptionsCount;
    UINT16           ContextsCount;
    OneOfContext     Contexts[CONTEXTS_MAX];
    UINT32           ListHeaderIndex;
    
    EFI_STATUS Status = EFI_SUCCESS;
    
    OptionsCount = 0;
    ContextsCount = 0;
    ListHeaderIndex = 0;
    
    // For Each Handle

    for (EFI_HII_HANDLE* h = (EFI_HII_HANDLE*) handles; (*h != NULL) && (ContextsCount < CONTEXTS_MAX); h++, ListHeaderIndex++) {

        ListHeaders[ListHeaderIndex] = HiiExportPackageLists(*h);
        
        if (ListHeaders[ListHeaderIndex] != NULL) {
            
            if (IS_VERBOSE()) {
                Print (L"Package List: ");
                PrintGuid (&ListHeaders[ListHeaderIndex]->PackageListGuid);
                Print (L"\n");
            }
            
            // First package in list
            EFI_HII_PACKAGE_HEADER* PkgHeader = PADD(ListHeaders[ListHeaderIndex], sizeof(EFI_HII_PACKAGE_LIST_HEADER));

            // For each package in list
            while (ContextsCount < CONTEXTS_MAX) {

                if (IS_VERBOSE()) {
                    Print (L"Package Type: %02X ", PkgHeader->Type);
                }
                
                if (PkgHeader->Type == EFI_HII_PACKAGE_END) {
                    break;
                }
                else if (PkgHeader->Type == EFI_HII_PACKAGE_FORMS) {
                    
                    EFI_IFR_OP_HEADER* IfrHeader = PADD(PkgHeader, sizeof(EFI_HII_PACKAGE_HEADER));
                    
                    if (IfrHeader->OpCode == EFI_IFR_FORM_SET_OP) { // Form Definition must start with FORM_SET_OP
                        
                        // Print some Info
                        if (IS_VERBOSE()) {
                            Print (L"Form: ");
                            PrintGuid ((void*) &(((EFI_IFR_FORM_SET*) IfrHeader)->Guid));
                        }
                        
                        if (IfrHeader->Length >= 16 + sizeof(EFI_IFR_FORM_SET)) {
                            if (IS_VERBOSE()) {
                                Print (L" Class Guid: ");
                                PrintGuid (PADD(IfrHeader, sizeof(EFI_IFR_FORM_SET)));
                                Print(L"\n");
                            }
                            
                            // Checkup for Setup Form
                            if (CompareGuid (&gEfiHiiPlatformSetupFormsetGuid, PADD(IfrHeader, sizeof(EFI_IFR_FORM_SET)))) {
                                
                                Contexts[ContextsCount].searchText = SearchString;
                                Contexts[ContextsCount].efiHandle = *h;
                                Contexts[ContextsCount].listHeader = ListHeaders[ListHeaderIndex];
                                Contexts[ContextsCount].pkgHeader = PkgHeader;
                                Contexts[ContextsCount].firstIfrHeader = PADD(IfrHeader, IfrHeader->Length);
                                Contexts[ContextsCount].ifrVarStore = NULL;
                                Contexts[ContextsCount].ifrOneOf = NULL;
                                Contexts[ContextsCount].stopAt = DONT_STOP_AT;
                                Contexts[ContextsCount].count = OptionsCount;
                                
                                DoForEachOpCode (Contexts[ContextsCount].firstIfrHeader, EFI_IFR_ONE_OF_OP, NULL, &Contexts[ContextsCount], HandleOneOf);
                                
                                if (Contexts[ContextsCount].count != OptionsCount) {
                                    OptionsCount = Contexts[ContextsCount].count;
                                    ContextsCount++;
                                }
                            }
                        }
                    }
                }
                PkgHeader = PADD(PkgHeader, PkgHeader->Length);
            } // For each package in list
            
            if (IS_VERBOSE()) {
                Print(L"\n");
            }
        } // ListHeader End
    }  // For Each Handle
    
    if (IS_VERBOSE()) {
        Print (L"Context count: %x Options count %x\n", ContextsCount, OptionsCount);
    }
    
    if (IS_INTERACTIVE() || IS_LOCK() || IS_UNLOCK()) {
        if (OptionsCount > 9 || ContextsCount == CONTEXTS_MAX) {
            Print (L"Too many corresponding BIOS Options found. Try a different search string using interactive mode.\n");
        }
        else if (OptionsCount == 0) {
            Print (L"No corresponding BIOS Options found. Try a different search string using interactive mode.\n");
        }
        else {
            CHAR16 key = '1';
            
            if (OptionsCount > 1) {
                do {
                    Print (L"\nEnter choice (1..%x) ? ", OptionsCount);
                    key = ReadAnyKey();
                } while ((key < '1') && (key > '0' + OptionsCount) && (key != 0x1B));
                
                Print (L"\n");
            }
            
            if (key != 0x1B) {
                UINT8 stop;
                UINT32 i;
                UINT16 Index = key - '0';
                
                for (i = 0; i < ContextsCount; i++) {
                    if (Contexts[i].count >= Index) {
                        Contexts[i].count = (i == 0) ? 0 : (Contexts[i - 1].count);
                        Contexts[i].stopAt = Index;
                        Contexts[i].ifrOneOf = NULL;
                        
                        DoForEachOpCode (Contexts[i].firstIfrHeader, EFI_IFR_ONE_OF_OP, &stop, &Contexts[i], HandleOneOf);
                        
                        if (Contexts[i].ifrOneOf != NULL) {
                            HandleOneVariable (&Contexts[i]);
                        }
                        break;
                    }
                }
            }
        }
    }
    
    for (ListHeaderIndex = 0; ListHeaderIndex < ListHeaderCount; ListHeaderIndex++) {
        if (ListHeaders[ListHeaderIndex] != NULL) {
            FreePool (ListHeaders[ListHeaderIndex]);
        }
    }
    
    return Status;
    
}
EFI_STATUS SearchForString (
                            IN EFI_STRING SearchString
                            ) {
    EFI_HII_HANDLE * handles;
    
    EFI_STATUS Status = EFI_SUCCESS;
    
        if (IS_INTERACTIVE()) {
            SearchString = ModifySearchString(SearchString);
        }
        
        handles = HiiGetHiiHandles (NULL);
        
        if (handles == NULL) {
            Print (L"Could not retrieve HiiHandles.\n");
            Status = EFI_OUT_OF_RESOURCES;
        }
        else {

            UINT32 ListHeaderCount;
            
            ListHeaderCount = 0;
            for (EFI_HII_HANDLE* h = (EFI_HII_HANDLE*) handles; *h != NULL; h++) {
                ListHeaderCount++;
            }

            // Keep list alive 'til program finishes.
            // So that all lists can be searched, the results be displayed together.
            // And from all those one Option will be selected to be changed
            EFI_HII_PACKAGE_LIST_HEADER **ListHeaders = AllocatePool (sizeof(*ListHeaders) * ListHeaderCount);
            
            if (ListHeaders != NULL) {
                Status = WalkListHeaders(handles, ListHeaders, ListHeaderCount, SearchString);
                FreePool (ListHeaders);
            }
            else {
                Print (L"Could not allocate memory.\n");
                Status = EFI_OUT_OF_RESOURCES;
            }

        } // Handling Handles
    return Status;
}

EFI_STATUS
EFIAPI
UefiMain (
          IN EFI_HANDLE        ImageHandle,
          IN EFI_SYSTEM_TABLE  *SystemTable
          )
{
    EFI_STATUS Status = InterpretArguments();
    
    if (!EFI_ERROR(Status)) {
        Status = VerifyMSRE2 (ImageHandle, SystemTable);
    }
    
    if (!EFI_ERROR(Status)) {
        Print(L"\nBIOS Options:\n");

        EFI_STRING SearchString = AsciiStrCopyToUnicode ("cfg", 0);

        if (SearchString != NULL) {
            Status = SearchForString (SearchString);
            FreePool (SearchString);
        }
    } else {
        Print (L"Could not allocate memory. Function not available.\n");
    }
    
    Print (L"Press any key. \n");
    ReadAnyKey();
    return Status;
}
