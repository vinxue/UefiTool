/** @file
    A simple UEFI tool for debugging.
**/

#include "UefiTool.h"

EFI_MP_SERVICES_PROTOCOL          *mMpService;
EFI_PROCESSOR_INFORMATION         *mProcessorLocBuf;
UINTN                             mProcessorNum;
UINTN                             mBspIndex;

CHAR16
ToUpper (
  CHAR16  a
  )
{
  if ('a' <= a && a <= 'z') {
    return (CHAR16) (a - 0x20);
  } else {
    return a;
  }
}

VOID
EFIAPI
StrUpr (
  IN OUT CHAR16  *Str
  )
{
  while (*Str) {
    *Str = ToUpper (*Str);
    Str += 1;
  }
}

//
// Bits definition of Command flag list
//
#define OPCODE_RDMSR_BIT                      BIT0
#define OPCODE_WRMSR_BIT                      BIT1
#define OPCODE_CPUID_BIT                      BIT2
#define OPCODE_ALLPROCESSOR_BIT               BIT3
#define OPCODE_PROCESSOR_INDEX_BIT            BIT4

typedef struct {
  // MSR
  UINT32                    MsrIndex;
  UINT64                    MsrValue;

  // CPUID
  UINT32                    CpuIdIndex;
  UINT32                    CpuIdSubIndex;

  UINTN                     ProcessorIndex;

} UEFI_TOOL_CONTEXT;

UEFI_TOOL_CONTEXT    gUtContext;

VOID
EFIAPI
ShowHelpInfo(
  VOID
  )
{
  Print (L"Help info:\n");
  Print (L"  UefiTool.efi -H\n\n");
  Print (L"Read MSR register:\n");
  Print (L"  UefiTool.efi RDMSR [MSRIndex] [OPTION: -A | -P]\n\n");
  Print (L"Write MSR register:\n");
  Print (L"  UefiTool.efi WRMSR [MSRIndex] [MSRValue]\n\n");
  Print (L"Read CPUID:\n");
  Print (L"  UefiTool.efi CPUID [CPUID_Index] [CPUID_SubIndex]\n\n");
}

/**
  Get All processors EFI_CPU_LOCATION in system. LocationBuf is allocated inside the function
  Caller is responsible to free LocationBuf.

  @param[out] LocationBuf          Returns Processor Location Buffer.
  @param[out] Num                  Returns processor number.

  @retval EFI_SUCCESS              Operation completed successfully.
  @retval EFI_UNSUPPORTED       MpService protocol not found.

**/
EFI_STATUS
GetProcessorsCpuLocation (
  VOID
  )
{
  EFI_STATUS                        Status;
  UINTN                             EnabledProcessorNum;
  UINTN                             Index;

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  &mMpService
                  );
  if (EFI_ERROR (Status)) {
    //
    // MP protocol is not installed
    //
    return EFI_UNSUPPORTED;
  }

  Status = mMpService->GetNumberOfProcessors (
                         mMpService,
                         &mProcessorNum,
                         &EnabledProcessorNum
                         );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof(EFI_PROCESSOR_INFORMATION) * mProcessorNum,
                  (VOID **) &mProcessorLocBuf
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get each processor Location info
  //
  for (Index = 0; Index < mProcessorNum; Index++) {
    Status = mMpService->GetProcessorInfo (
                           mMpService,
                           Index,
                           &mProcessorLocBuf[Index]
                           );
    if (EFI_ERROR (Status)) {
      FreePool(mProcessorLocBuf);
      return Status;
    }
  }

  Status = mMpService->WhoAmI (
                         mMpService,
                         &mBspIndex
                         );
  return Status;
}

VOID
ApUtReadMsr (
  IN UINTN             *Index
  )
{
  UINT64             MsrData;

  MsrData = AsmReadMsr64 (gUtContext.MsrIndex);
  Print (L"RDMSR[0x%X][ProcNum: %d S%d_C%d_T%d]: [64b] 0x%016lX\n", gUtContext.MsrIndex, *Index,
    mProcessorLocBuf[*Index].Location.Package,
    mProcessorLocBuf[*Index].Location.Core,
    mProcessorLocBuf[*Index].Location.Thread,
    MsrData);
}

VOID
ApUtReadCpuId (
  IN UINTN             *Index
  )
{
  UINT32             RegisterEax;
  UINT32             RegisterEbx;
  UINT32             RegisterEcx;
  UINT32             RegisterEdx;

  AsmCpuidEx (gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex, &RegisterEax, &RegisterEbx, &RegisterEcx, &RegisterEdx);
  Print (L"CPUID[S%d_C%d_T%d]: Index: 0x%X     SubIndex: 0x%X\n",
  mProcessorLocBuf[*Index].Location.Package,
  mProcessorLocBuf[*Index].Location.Core,
  mProcessorLocBuf[*Index].Location.Thread,
  gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex);
  Print (L"EAX = 0x%08X\n", RegisterEax);
  Print (L"EBX = 0x%08X\n", RegisterEbx);
  Print (L"ECX = 0x%08X\n", RegisterEcx);
  Print (L"EDX = 0x%08X\n\n", RegisterEdx);
}

VOID
EFIAPI
UefiToolRoutine (
  UINT64             Opcode
  )
{
  UINT64             MsrData;
  UINT32             RegisterEax;
  UINT32             RegisterEbx;
  UINT32             RegisterEcx;
  UINT32             RegisterEdx;
  UINTN              Index;

  if (Opcode == OPCODE_RDMSR_BIT) {
    MsrData = AsmReadMsr64 (gUtContext.MsrIndex);
    Print (L"RDMSR[0x%X]: 0x%016lX\n", gUtContext.MsrIndex, MsrData);
  }

  if (Opcode == OPCODE_WRMSR_BIT) {
    MsrData = AsmWriteMsr64 (gUtContext.MsrIndex, gUtContext.MsrValue);
    Print (L"WR Data 0x%016lX to MSR[0x%X]\n", gUtContext.MsrValue, gUtContext.MsrIndex);
  }

  if (Opcode == OPCODE_CPUID_BIT) {
    AsmCpuidEx (gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex, &RegisterEax, &RegisterEbx, &RegisterEcx, &RegisterEdx);
    Print (L"CPUID Index: 0x%X     SubIndex: 0x%X\n", gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex);
    Print (L"EAX = 0x%08X\n", RegisterEax);
    Print (L"EBX = 0x%08X\n", RegisterEbx);
    Print (L"ECX = 0x%08X\n", RegisterEcx);
    Print (L"EDX = 0x%08X\n", RegisterEdx);
  }
  if (Opcode == (OPCODE_RDMSR_BIT | OPCODE_PROCESSOR_INDEX_BIT)) {
    Index = gUtContext.ProcessorIndex;
    if (Index == mBspIndex) {
      Print (L"RDMSR[0x%X][ProcNum: %d S%d_C%d_T%d]: [64b] 0x%16lX\n", gUtContext.MsrIndex, Index,
        mProcessorLocBuf[Index].Location.Package,
        mProcessorLocBuf[Index].Location.Core,
        mProcessorLocBuf[Index].Location.Thread,
        AsmReadMsr64 (gUtContext.MsrIndex));
    } else {
      mMpService->StartupThisAP (
                    mMpService,
                    (EFI_AP_PROCEDURE) ApUtReadMsr,
                    Index,
                    NULL,
                    0,
                    &Index,
                    NULL
                    );
    }
  }

  if (Opcode == (OPCODE_CPUID_BIT | OPCODE_PROCESSOR_INDEX_BIT)) {
    Index = gUtContext.ProcessorIndex;
    if (Index == mBspIndex) {
      AsmCpuidEx (gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex, &RegisterEax, &RegisterEbx, &RegisterEcx, &RegisterEdx);
      Print (L"CPUID[S%d_C%d_T%d]: Index: 0x%X     SubIndex: 0x%X\n",
      mProcessorLocBuf[Index].Location.Package,
      mProcessorLocBuf[Index].Location.Core,
      mProcessorLocBuf[Index].Location.Thread,
      gUtContext.CpuIdIndex, gUtContext.CpuIdSubIndex);
      Print (L"EAX = 0x%08X\n", RegisterEax);
      Print (L"EBX = 0x%08X\n", RegisterEbx);
      Print (L"ECX = 0x%08X\n", RegisterEcx);
      Print (L"EDX = 0x%08X\n", RegisterEdx);
    } else {
      mMpService->StartupThisAP (
                    mMpService,
                    (EFI_AP_PROCEDURE) ApUtReadCpuId,
                    Index,
                    NULL,
                    0,
                    &Index,
                    NULL
                    );
    }
  }

  if (Opcode == (OPCODE_RDMSR_BIT | OPCODE_ALLPROCESSOR_BIT)) {
    for (Index = 0; Index < mProcessorNum; Index++) {
      if (Index == mBspIndex) {
        Print (L"RDMSR[0x%X][ProcNum: %d S%d_C%d_T%d]: [64b] 0x%16lX\n", gUtContext.MsrIndex, Index,
          mProcessorLocBuf[Index].Location.Package,
          mProcessorLocBuf[Index].Location.Core,
          mProcessorLocBuf[Index].Location.Thread,
          AsmReadMsr64 (gUtContext.MsrIndex));
      } else {
        mMpService->StartupThisAP (
                      mMpService,
                      (EFI_AP_PROCEDURE) ApUtReadMsr,
                      Index,
                      NULL,
                      0,
                      &Index,
                      NULL
                      );
      }
    }
  }

}

/**
UEFI application entry point which has an interface similar to a
standard C main function.

The ShellCEntryLib library instance wrappers the actual UEFI application
entry point and calls this ShellAppMain function.

@param  ImageHandle  The image handle of the UEFI Application.
@param  SystemTable  A pointer to the EFI System Table.

@retval  0               The application exited normally.
@retval  Other           An error occurred.

**/
INTN
EFIAPI
ShellAppMain (
  IN UINTN                     Argc,
  IN CHAR16                    **Argv
  )
{
  EFI_STATUS                Status;
  UINT8                     Index;
  UINT64                    Opcode;

  Print (L"\nUEFI Debug Tool. Version: 1.0.0.1\n");
  Print (L"Copyright (c) 2017 - 2018 Gavin Xue. All rights reserved.\n\n");

  if (Argc == 1) {
    ShowHelpInfo ();
    return EFI_INVALID_PARAMETER;
  }

  else if (Argc == 2) {
    StrUpr (Argv[1]);
    if ((!StrCmp (Argv[1], L"/H")) || (!StrCmp (Argv[1], L"-H")) ||
        (!StrCmp (Argv[1], L"/?")) || (!StrCmp (Argv[1], L"-?"))) {
      ShowHelpInfo ();
      return EFI_SUCCESS;
    } else {
      Print (L"Invaild parameter.\n");
      return EFI_INVALID_PARAMETER;
    }
  }

  Opcode = 0x0;
  SetMem (&gUtContext, sizeof (UEFI_TOOL_CONTEXT), 0x0);

  Status = GetProcessorsCpuLocation ();

  Index = 1;
  while (TRUE) {
    if (Index + 1 > Argc) {
      break;
    }

    StrUpr (Argv[Index]);

    if ((Index + 1 < Argc) && (StrCmp (Argv[Index], L"RDMSR") == 0)) {
      gUtContext.MsrIndex  = (UINT32) StrHexToUintn (Argv[Index + 1]);
      Index += 2;
      Opcode |= OPCODE_RDMSR_BIT;
    } else if ((Index + 2 < Argc) && (StrCmp (Argv[Index], L"WRMSR") == 0)) {
      gUtContext.MsrIndex  = (UINT32) StrHexToUintn (Argv[Index + 1]);
      gUtContext.MsrValue  = StrHexToUintn (Argv[Index + 2]);
      Index += 3;
      Opcode |= OPCODE_WRMSR_BIT;
    } else if ((Index + 2 < Argc) && (StrCmp (Argv[Index], L"CPUID") == 0)) {
      gUtContext.CpuIdIndex    = (UINT32) StrHexToUintn (Argv[Index + 1]);
      gUtContext.CpuIdSubIndex = (UINT32) StrHexToUintn (Argv[Index + 2]);
      Index += 3;
      Opcode |= OPCODE_CPUID_BIT;
    } else if ((Index < Argc) && (StrCmp (Argv[Index], L"-A") == 0)) {
      Index++;
      Opcode |= OPCODE_ALLPROCESSOR_BIT;
    } else if ((Index + 1 < Argc) && (StrCmp (Argv[Index], L"-P") == 0)) {
      gUtContext.ProcessorIndex  = StrHexToUintn (Argv[Index + 1]);
      Index += 2;
      Opcode |= OPCODE_PROCESSOR_INDEX_BIT;
    } else {
      Index++;
    }
  }

  UefiToolRoutine (Opcode);

  return 0;
}
