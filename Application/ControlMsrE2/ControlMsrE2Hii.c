/** @file
  Control MSR 0xE2 Hii HiiDatabase related stuff

Copyright (c) 2020, Brumbaer. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "ControlMsrE2.h"

typedef struct VAR_STORE_CONTEXT_ {
  EFI_VARSTORE_ID    Id;
  EFI_IFR_VARSTORE   *VarStoreHeader;
} VAR_STORE_CONTEXT;

EFI_HII_PACKAGE_LIST_HEADER *
HiiExportPackageLists (
  IN EFI_HII_HANDLE Handle
  )
{
  EFI_STATUS                    Status;
  UINTN                         BufferSize;
  EFI_HII_PACKAGE_LIST_HEADER   *Result;

  BufferSize = 0;

  Status = gHiiDatabase->ExportPackageLists (gHiiDatabase, Handle, &BufferSize, NULL);

  if (Status == EFI_BUFFER_TOO_SMALL && BufferSize > 0) {
    Result = (EFI_HII_PACKAGE_LIST_HEADER *) AllocatePool (BufferSize);

    if (Result == NULL) {
      return NULL;
    }

    Status = gHiiDatabase->ExportPackageLists (gHiiDatabase, Handle, &BufferSize, Result);

    if (EFI_ERROR (Status)) {
      FreePool (Result);
      return NULL;
    }

    return Result;
  }
  return NULL;
}

EFI_IFR_OP_HEADER *
DoForEachOpCode (
  IN     EFI_IFR_OP_HEADER   *Header,
  IN     UINT8               OpCode,
  IN OUT BOOLEAN             *Stop     OPTIONAL,
  IN     VOID                *Context,
  IN     OP_CODE_HANDLER     Handler
  )
{
  while (Stop == NULL || !(*Stop)) {
    if (Header->OpCode == EFI_IFR_END_OP) {
      return Header;
    }

    if (Header->OpCode == OpCode) {
      Handler (Header, Stop, Context);
      if (Stop != NULL && *Stop) {
        return Header;
      }
    }

    if (Header->Scope) {
      Header = DoForEachOpCode (
                 PADD (Header, Header->Length),
                 OpCode,
                 Stop,
                 Context,
                 Handler
                 );
    }

    Header = PADD (Header, Header->Length);
  }

  return Header;
}

VOID
HandleVarStore (
  IN     EFI_IFR_OP_HEADER   *IfrHeader,
  IN OUT BOOLEAN             *Stop       OPTIONAL,
  IN OUT VOID                *Context
  )
{
  VAR_STORE_CONTEXT   *Ctx;
  EFI_IFR_VARSTORE    *VarStore;

  Ctx = Context;
  VarStore = (EFI_IFR_VARSTORE *) IfrHeader;

  if (VarStore->VarStoreId == Ctx->Id) {
    Ctx->VarStoreHeader = VarStore;
    if (Stop != NULL) {
      *Stop = TRUE;
    }
  }
}

EFI_IFR_VARSTORE *
GetVarStore (
  IN EFI_IFR_OP_HEADER   *Header,
  IN EFI_VARSTORE_ID     Id
  )
{
  BOOLEAN             Stop;
  VAR_STORE_CONTEXT   Context;

  Stop = FALSE;
  Context.Id = Id;
  Context.VarStoreHeader = NULL;

  DoForEachOpCode (Header, EFI_IFR_VARSTORE_OP, &Stop, &Context, HandleVarStore);

  return Context.VarStoreHeader;
}

VOID
HandleOneOf (
  IN     EFI_IFR_OP_HEADER   *IfrHeader,
  IN OUT BOOLEAN             *Stop       OPTIONAL,
  IN OUT VOID                *Context
  )
{
  ONE_OF_CONTEXT     *Ctx;
  UINT8              *Data;
  UINT8              *VarPointer;
  UINTN              DataSize;
  UINTN              VarSize;
  UINT16             OldContextCount;
  UINT64             VarStoreValue;
  EFI_STATUS         Status;
  EFI_IFR_VARSTORE   *IfrVarStore;
  EFI_IFR_ONE_OF     *IfrOneOf;
  EFI_STRING         HiiString;
  EFI_STRING         VarStoreName;

  Ctx = Context;
  IfrOneOf = (EFI_IFR_ONE_OF *) IfrHeader;
  HiiString = HiiGetString (Ctx->EfiHandle, IfrOneOf->Question.Header.Prompt, "en-US");

  if (HiiString == NULL) {
    DEBUG ((DEBUG_ERROR, "\nCouldn't allocate memory\n"));
    return;
  }

  IfrVarStore = GetVarStore (Ctx->FirstIfrHeader, IfrOneOf->Question.VarStoreId);
  if (IfrVarStore != NULL) {
    if (OcStriStr (HiiString, Ctx->SearchText) != NULL) {
      OldContextCount = Ctx->Count;

      if (Ctx->IfrOneOf == NULL) {
        Ctx->IfrOneOf = IfrOneOf;
        Ctx->IfrVarStore = IfrVarStore;
        Ctx->Count++;
      } else {  ///< Skip identical Options
        if (Ctx->IfrOneOf->Question.VarStoreId != IfrOneOf->Question.VarStoreId
         || Ctx->IfrOneOf->Question.VarStoreInfo.VarOffset != IfrOneOf->Question.VarStoreInfo.VarOffset) {
          Ctx->IfrOneOf = IfrOneOf;
          Ctx->IfrVarStore = IfrVarStore;
          Ctx->Count++;
        }
      }

      if (Ctx->Count == Ctx->StopAt && Stop != NULL) {
        Ctx->IfrOneOf = IfrOneOf;
        Ctx->IfrVarStore = IfrVarStore;
        Ctx->Count = 1;
        *Stop = TRUE;
      } else if (OldContextCount != Ctx->Count && Ctx->StopAt == DONT_STOP_AT) {
        VarStoreName = AsciiStrCopyToUnicode ((CHAR8 *) IfrVarStore->Name, 0);

        Print (
          L"%X. %02X %04X %04X /%s/ VarStore Name: %s",
          Ctx->Count,
          IfrOneOf->Header.OpCode,
          IfrOneOf->Question.VarStoreInfo.VarName,
          IfrOneOf->Question.VarStoreId,
          HiiString,
          VarStoreName
          );

        DataSize = 0;
        Status = gRT->GetVariable (
                        VarStoreName,
                        (VOID *) &IfrVarStore->Guid,
                        NULL,
                        &DataSize,
                        NULL
                        );

        if (Status == EFI_BUFFER_TOO_SMALL) {
          Data = AllocatePool (DataSize);
          if (Data != NULL) {
            Status = gRT->GetVariable (
                            VarStoreName,
                            (VOID *) &IfrVarStore->Guid,
                            NULL,
                            &DataSize,
                            Data
                            );

            if (!EFI_ERROR (Status)) {
              VarSize = sizeof (EFI_IFR_ONE_OF) - IfrOneOf->Header.Length;
              VarSize = 8 - (VarSize / 3);

              VarPointer = Data + IfrOneOf->Question.VarStoreInfo.VarOffset;
              switch (VarSize) {
              case 1:
                VarStoreValue = *VarPointer;
                break;
              case 2:
                VarStoreValue = *(UINT16 *) (VarPointer);
                break;
              case 4:
                VarStoreValue = *(UINT32 *) (VarPointer);
                break;
              default:
                VarStoreValue = *(UINT64 *) (VarPointer);
                break;
              }

              Print (L" Value: value %X", VarStoreValue);
            }
            FreePool (Data);
          }  ///< Allocate
        }  ///< GetVariable
        FreePool (VarStoreName);
        Print (L"\n");
      }
    }
  }
  FreePool (HiiString);
}

VOID
HandleOneVariable (
  IN OUT ONE_OF_CONTEXT *Context
  )
{
  EFI_STATUS        Status;
  EFI_STRING        HiiString;
  UINTN             DataSize;
  UINT8             *Data;
  UINT32            Attributes;
  UINTN             VarSize;
  UINT8             *VarPointer;
  UINT64            VarStoreValue;
  UINT64            NewValue;

  HiiString = HiiGetString (Context->EfiHandle, Context->IfrOneOf->Question.Header.Prompt, "en-US");
  if (HiiString) {
    Print (L"\nBIOS Option found: %s\n", HiiString);
    FreePool (HiiString);
  }

  HiiString = AsciiStrCopyToUnicode ((CHAR8 *) Context->IfrVarStore->Name, 0);

  if (HiiString == NULL) {
    DEBUG ((DEBUG_ERROR, "\nCouldn't allocate memory\n"));
    return;
  }

  VarSize = sizeof (EFI_IFR_ONE_OF) - Context->IfrOneOf->Header.Length;
  VarSize = 8 - (VarSize / 3);

  Print (
    L"In VarStore \"%s\" GUID: %g Offset: %04X Size: %X ",
    HiiString,
    &Context->IfrVarStore->Guid,
    Context->IfrOneOf->Question.VarStoreInfo.VarOffset,
    VarSize
    );

  DataSize = 0;

  Status = gRT->GetVariable (
                  HiiString,
                  (VOID *) &Context->IfrVarStore->Guid,
                  &Attributes,
                  &DataSize,
                  NULL
                  );

  if (Status == EFI_BUFFER_TOO_SMALL) {
    if ((Data = AllocatePool (DataSize)) != NULL) {
      Status = gRT->GetVariable (
                      HiiString,
                      (VOID *) &Context->IfrVarStore->Guid,
                      &Attributes,
                      &DataSize,
                      Data
                      );

      if (!EFI_ERROR (Status)) {
        VarPointer = Data + Context->IfrOneOf->Question.VarStoreInfo.VarOffset;
        switch (VarSize) {
        case 1:
          VarStoreValue = *VarPointer;
          break;
        case 2:
          VarStoreValue = *(UINT16 *) (VarPointer);
          break;
        case 4:
          VarStoreValue = *(UINT32 *) (VarPointer);
          break;
        default:
          VarStoreValue = *(UINT64 *) (VarPointer);
          break;
        }

        Print (L"Value: value %X\n", VarStoreValue);

        NewValue = (IS_INTERACTIVE ()) ? (VarStoreValue) ? 0 : 1 : (IS_LOCK ()) ? 1 : (IS_UNLOCK ()) ? 0 : VarStoreValue;

        if (NewValue != VarStoreValue) {
          if (IS_INTERACTIVE ()) {
            Print (L"Do you want to toggle the value y/n ?");
          } else if (IS_LOCK ()) {
            Print (L"Do you want to set the value y/n ?");
          } else {
            Print (L"Do you want to clear the value y/n ?");
          }

          if (ReadYN ()) {
            switch (VarSize) {
              case 1:
                *VarPointer = (UINT8)NewValue;
                break;
              case 2:
                *(UINT16 *) (VarPointer) = (UINT16)NewValue;
                break;
              case 4:
                *(UINT32 *) (VarPointer) = (UINT32)NewValue;
                break;
              case 8:
                *(UINT64 *) (VarPointer) = NewValue;
                break;
              default:
                break;
            }

            Status = gRT->SetVariable (
                            HiiString,
                            (VOID *) &Context->IfrVarStore->Guid,
                            Attributes,
                            DataSize,
                            Data
                            );

            if (!EFI_ERROR (Status)) {
              Print (L"\nDone. You will have to reboot for the change to take effect.\n");
            } else {
              DEBUG ((DEBUG_ERROR, "\nProblem writing variable.\n"));
            }
          } else {
            Print (L"\n");
          }
        } else {
          DEBUG ((DEBUG_ERROR, "Value is as wanted already. No action required.\n"));
        }
      } else {
        DEBUG ((DEBUG_ERROR, "\nCouldn't read Data\n"));
      }
      FreePool (Data);
    } else {
      DEBUG ((DEBUG_ERROR, "\nCouldn't allocate memory\n"));
    }
  } else {
    DEBUG ((DEBUG_ERROR, "\nCouldn't find Variable.\n"));
  }
  FreePool (HiiString);
}