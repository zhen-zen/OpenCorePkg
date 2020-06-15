/** @file
  Play beep.

Copyright (c) 2020, vit9696. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Guid/GlobalVariable.h>
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/OcDebugLogLib.h>
#include <Library/OcMiscLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS        Status;
  UINTN             Argc;
  CHAR16            **Argv;
  CHAR16            *Mode;
  EFI_RESET_TYPE    ResetMode;
  UINT64            OsIndications;
  UINT32            Attr;
  UINTN             DataSize;

  Status = GetArguments (&Argc, &Argv);
  if (!EFI_ERROR (Status) && Argc >= 2) {
    Mode = Argv[1];
  } else {
    DEBUG ((DEBUG_INFO, "OCRST: Assuming default to be ResetCold - %r\n", Status));
    Mode = L"ColdReset";
  }

  if (StrCmp (Mode, L"Firmware") == 0) {
    DEBUG ((DEBUG_INFO, "OCRST: Entering firmware...\n"));
    DataSize = sizeof (OsIndications);
    Status = gRT->GetVariable (
                    EFI_OS_INDICATIONS_SUPPORT_VARIABLE_NAME,
                    &gEfiGlobalVariableGuid,
                    &Attr,
                    &DataSize,
                    &OsIndications
                    );
    if (!EFI_ERROR (Status)) {
      if ((OsIndications & EFI_OS_INDICATIONS_BOOT_TO_FW_UI) != 0) {
        DataSize = sizeof (OsIndications);
        Status = gRT->GetVariable (
                        EFI_OS_INDICATIONS_VARIABLE_NAME,
                        &gEfiGlobalVariableGuid,
                        &Attr,
                        &DataSize,
                        &OsIndications
                        );
        if (!EFI_ERROR (Status)) {
          OsIndications |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
        } else {
          OsIndications = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
        }
        Status = gRT->SetVariable (
                        EFI_OS_INDICATIONS_VARIABLE_NAME,
                        &gEfiGlobalVariableGuid,
                        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                        sizeof (OsIndications),
                        &OsIndications
                        );
      }
    }
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "OCRST: Failed to enter firmware - %r\n", Status));
      return EFI_UNSUPPORTED;
    }
    Mode = L"ColdReset";
  }

  if (StrCmp (Mode, L"ColdReset") == 0) {
    DEBUG ((DEBUG_INFO, "OCRST: Perform cold reset...\n"));
    ResetMode = EfiResetCold;
  } else if (StrCmp (Mode, L"WarmReset") == 0) {
    DEBUG ((DEBUG_INFO, "OCRST: Perform warm reset...\n"));
    ResetMode = EfiResetWarm;
  } else if (StrCmp (Mode, L"Shutdown") == 0) {
    DEBUG ((DEBUG_INFO, "OCRST: Perform shutdown...\n"));
    ResetMode = EfiResetShutdown;
  } else {
    DEBUG ((DEBUG_INFO, "OCRST: Unknown argument %s, defaulting to cold reset...\n", Mode));
    ResetMode = EfiResetCold;
  }

  gRT->ResetSystem (
    ResetMode,
    EFI_SUCCESS,
    0,
    NULL
    );

  DEBUG ((DEBUG_INFO, "OCRST: Failed to reset, trying direct\n"));

  DirectRestCold ();

  DEBUG ((DEBUG_INFO, "OCRST: Failed to reset directly, entering dead loop\n"));

  CpuDeadLoop ();

  return EFI_SUCCESS; ///< Unreachable
}
