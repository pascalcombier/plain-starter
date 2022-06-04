/**
 *  +----------+---------------------------------------------------------------+
 *  | Info     | Value                                                         |
 *  +----------+---------------------------------------------------------------+
 *  | Filename | plainstarter-win32.c                                          |
 *  | Project  | plain-starter                                                 |
 *  | License  | Simplified BSD License (details in attached LICENSE file)     |
 *  +----------+---------------------------------------------------------------+
 *  | Copyright (C) 2014-2022 Pascal COMBIER <pascal.combier@outlook.com>      |
 *  +--------------------------------------------------------------------------+
 *
 * Plainstarter is a lightweight starter for Microsoft Windows programs. The
 * purpose is to expose a native Windows program even if the underlying
 * implementation is using third-party programs or interpreters such as Python,
 * Perl or many others. It can be used in place of batch scripts, allowing the
 * programmer to focus on the implementation of his software.
 *
 * Interpreted languages usually allow the development of Graphical User
 * Interfaces. On Windows operating system, you will usually get an additionnal
 * terminal window which could be annoying for the end-user. Plainstarter make
 * it easy to hide this terminal window (see PLAINSTARTER_OPTIONS).
 *
 * Additionnally, Plainstarter make it easy to register environment
 * variables. It is straight-forward to create variables pointing to
 * sub-directories. This is important for programs relying on third-party
 * dynamic libraries. Such programs will typically prepend or append the
 * sub-directories containing the DLLs to the PATH variable.
 *
 * Plainstarter read a configuration file containing the command line to
 * execute. The executable can be named according to the user choice, but the
 * name of the configuration file should match the binary name.
 *
 * Examples:
 * - plainstarter.exe will try to load the configuration file
 * "configs\plainstarter.cfg", then "config\plainstarter.cfg" and finally
 * "plainstarter.cfg".
 * - my-app.exe will try to load the configuration file "configs\my-app.cfg",
 * then "config\my-app.cfg" and finally "my-app.cfg".
 *
 * The configuration file is a simple list of environment variables to setup and
 * export to the child processes:
 * PLAINSTARTER_OPTIONS=option-1 option-2
 * VAR1=VAL1
 * VAR2=VAL2
 * VAR3=VAL3
 * PATH=bin\my-dir\dlls;%PATH%
 * PLAINSTARTER_CMD_LINE=command line
 *
 * Note: the variables can reference any of the Microsoft Windows environment
 * variables (ie. %PATH% or %APPDATA%). It is also possible to override the
 * value of these variables to prepend/append application-defined directory.
 *
 * Lines are processed one by one. For this reason, the command line probably
 * need to be set at the end of the file. Comments can be inserted using the
 * character '#'. Variable names or values longer than 1024 characters will be
 * ignored. Files larger than 10240 bytes will lead to an execution error.
 *
 * These special variables are used internally by Plainstarter and will not be
 * exported to the child processes.
 *
 * PLAINSTARTER_CMD_LINE
 * This is the command line to execute. All the parameters given to Plainstarter
 * will be appended to PLAINSTARTER_CMD_LINE. This behavior is required to
 * transmit command line options to the underlying programs. PLAINSTARTER_CMD_LINE
 * can reference any environment variable.
 *
 * PLAINSTARTER_OPTIONS
 * Contains a list of options to alter the execution of plainstarter.
 *
 * PLAINSTARTER_PROGNAME
 * This is the name of the executable without the '.exe' extension. This can be
 * used when the behavior of the underlying program depends on the name of the
 * executable.
 *
 * PLAINSTARTER_DIRECTORY
 * This is the absolute path of the directory containing the executable
 * file. This variable can be used to add the application-specific Dynamic
 * Libraries.
 *
 * PLAINSTARTER_OPTIONS
 * show-console
 * init-common-controls
 * monitor-process
 * debug
 */

/*---------------------*/
/* INCLUDES AND MACROS */
/*---------------------*/

#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <tchar.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>

#define PS_ARRAY_SIZE(array) ((sizeof(array)/sizeof(array[0])))

/*-----------*/
/* CONSTANTS */
/*-----------*/

/* Maximum size of the filename in characters, big-enough for NTFS
 * long-filenames */
static const size_t PS_MAX_FILENAME_LENGTH_CHAR = (size_t)32767;

/* Maximum size of the file buffer in bytes */
static const size_t PS_MAX_CONFIG_FILE_SIZE_BYTES = (size_t)10240;

static const TCHAR *PS_UNEXPECTED_ERROR = _T("Unexpected error");
static const TCHAR  PS_CONFIG_DIR_1[8]  = _T("configs\\");
static const TCHAR  PS_CONFIG_DIR_2[7]  = _T("config\\");
static const TCHAR  PS_CONFIG_DIR_3[1]  = _T("");

#define PS_MAX_LINE_LEN_BYTES ((unsigned int)1024)

/*------------------*/
/* GLOBAL VARIABLES */
/*------------------*/

/* Buffer which can contain the largest Windows PATH variable */
static TCHAR PS_BufferIn[32767];
static TCHAR PS_BufferOut[32767];

static BOOL PS_OPTION_INIT_COMMON_CONTROLS = FALSE;
static BOOL PS_OPTION_SHOW_CONSOLE         = FALSE;
static BOOL PS_OPTION_MONITOR_PROCESS      = FALSE;
static BOOL PS_OPTION_DEBUG                = FALSE;
static int  PS_LAST_EXEC_CODE              = EXIT_SUCCESS;

/*------------------------*/
/* UTILITY LIBC FUNCTIONS */
/*------------------------*/

static TCHAR *PS_StringAppend (TCHAR       *Out,
                               const TCHAR *InStart,
                               const TCHAR *InEnd)
{
  TCHAR       *po = Out;
  const TCHAR *pi = InStart;

  while (pi <= InEnd)
  {
    *po = *pi;
    pi++;
    po++;
  }

  return po;
}

static void PS_MessageAndExit (char         ErrorId,
                               const TCHAR *Message,
                               int          ErrorCode)
{
  DWORD BytesWritten;

  DWORD_PTR Args[] = {
    (DWORD_PTR)ErrorId
  };

  BytesWritten = FormatMessage(FORMAT_MESSAGE_FROM_STRING
                               | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                               _T("Error#%1!2.2d!"),
                               0,
                               0,
                               PS_BufferIn,
                               sizeof(PS_BufferIn),
                               (char **)Args);
  if (BytesWritten > 0)
  {
    MessageBox(NULL, Message, PS_BufferIn, MB_ICONERROR);
  }
  else
  {
    MessageBox(NULL, PS_UNEXPECTED_ERROR, _T("Error"), MB_ICONERROR);
  }

  ExitProcess(ErrorCode);
}

/*----------------*/
/* MAIN FUNCTIONS */
/*----------------*/

/* Return a string with the configuration filename.
 * <path>/<Prefix>/<basename>.cfg
 *
 * Input : bin\starter-x86_64.exe
 * Output: bin\[configs]\starter-x86_64.cfg
 */
static TCHAR *PS_GetConfigFilename (const TCHAR *Prefix,
                                    size_t       PrefixLength,
                                    TCHAR       *Filename)
{
  int    FilenameLen = lstrlen(Filename);
  size_t NewFilenameLength;

  TCHAR *Buffer;
  TCHAR *BufferEnd;
  TCHAR *p;

  TCHAR *ProgName;
  TCHAR *ProgNameEnd;

  NewFilenameLength = (PrefixLength + FilenameLen + 5) * sizeof(TCHAR);
  if (NewFilenameLength <= PS_MAX_FILENAME_LENGTH_CHAR)
  {
    Buffer = HeapAlloc(GetProcessHeap(), 0, NewFilenameLength);
    if (Buffer != NULL)
    {
      /* find the basename */
      ProgName    = 0;
      ProgNameEnd = Filename + FilenameLen - 1; /* end of the Filename */
      p           = ProgNameEnd;
      while (ProgName == 0)
      {
        if (p == Filename)
        {
          ProgName = p;
        }
        else if ((*p == _T('.') && (*ProgNameEnd != _T('.'))))
        {
          ProgNameEnd = p - 1;
        }
        else if (*p ==  _T('\\'))
        {
          ProgName = p + 1;
        }

        /* next char */
        p--;
      }

      BufferEnd = Buffer;

      /* Filename contains a directory */
      if (Filename != ProgName)
        BufferEnd = PS_StringAppend(BufferEnd, Filename, ProgName - 1);
      /* Non-empty prefix */
      if (PrefixLength > 1)
        BufferEnd = PS_StringAppend(BufferEnd, Prefix, (Prefix + PrefixLength - 1));
      BufferEnd = PS_StringAppend(BufferEnd, ProgName, ProgNameEnd);
      *BufferEnd++ = _T('.');
      *BufferEnd++ = _T('c');
      *BufferEnd++ = _T('f');
      *BufferEnd++ = _T('g');
      *BufferEnd++ = _T('\0');
    }
  }
  else
  {
    Buffer = NULL;
  }

  return Buffer;
}

/* Read a file and return a new allocated buffer with the data. No support for
 * large files because the size of a configuration file should be a few
 * kilobytes, smaller than PS_MAX_CONFIG_FILE_SIZE_BYTES.
 */
static TCHAR *PS_FileSlurp (TCHAR *Filename)
{
  HANDLE Infile;
  DWORD  InfileSize;
  DWORD  BytesRead;
  DWORD  BytesWritten;
  char  *Buffer = NULL;

  Infile = CreateFile(Filename,
                      GENERIC_READ,
                      FILE_SHARE_READ,
                      NULL,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL
                      | FILE_ATTRIBUTE_READONLY
                      | FILE_ATTRIBUTE_HIDDEN,
                      NULL);

  if (Infile != INVALID_HANDLE_VALUE)
  {
    InfileSize = GetFileSize(Infile, NULL);
    if ((InfileSize != INVALID_FILE_SIZE) && (InfileSize < PS_MAX_CONFIG_FILE_SIZE_BYTES))
    {
      Buffer = HeapAlloc(GetProcessHeap(), 0, ((InfileSize + 2) * sizeof(char)));
      if (Buffer)
      {
        if (ReadFile(Infile, Buffer, InfileSize, &BytesRead, NULL) == TRUE)
        {
          Buffer[InfileSize] = _T('\0');
        }
        else
        {
          PS_MessageAndExit(1, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
        }
      }
      else
      {
        PS_MessageAndExit(2, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
      }
    }
    else
    {
      DWORD_PTR Args[] = {
        (DWORD_PTR)Filename,
        (DWORD_PTR)InfileSize,
        (DWORD_PTR)PS_MAX_CONFIG_FILE_SIZE_BYTES
      };

      BytesWritten = FormatMessage(FORMAT_MESSAGE_FROM_STRING
                                   | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                   _T("Configuration file is too large.\n")
                                   _T("File: '%1!s!'\n")
                                   _T("Size: %2!d! bytes\n")
                                   _T("Software limit: %3!d! bytes"),
                                   0,
                                   0,
                                   PS_BufferIn,
                                   sizeof(PS_BufferIn),
                                   (char **)Args);
      if (BytesWritten > 0)
      {
        PS_MessageAndExit(3, PS_BufferIn, EXIT_FAILURE);
      }
      else
      {
        PS_MessageAndExit(4, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
      }
    }

    /* Release resources */
    CloseHandle(Infile);
  }

  return (TCHAR *)Buffer;
}

/* Return a new allocated string containing the directory where is located the
 * plainstarter binary
 */
static TCHAR *PS_LocateWin32BinaryDirectory (TCHAR  *Buffer,
                                             size_t  BufferLen)
{
  size_t  DirLength;
  TCHAR  *NewString;
  TCHAR  *Progname;
  TCHAR  *LastBackslash;
  TCHAR  *LastDot;
  TCHAR  *p;

  /* Retrieve the full path of the current executable file */
  if (GetModuleFileName(GetModuleHandle(NULL), Buffer, BufferLen) != 0)
  {
    /* Find the end of the string and last backslash location */
    LastBackslash = Buffer + BufferLen;
    LastDot       = Buffer + BufferLen;
    p             = Buffer;
    while (*p)
    {
      if (*p == _T('\\'))
      {
        LastBackslash = p;
      }
      else if (*p == _T('.'))
      {
        LastDot = p;
      }
      p++;
    }

    /* Set Progname */
    Progname = LastBackslash + 1;
    *LastDot = _T('\0');
    SetEnvironmentVariable(_T("PLAINSTARTER_PROGNAME"), Progname);

    /* Create a new string */
    DirLength = LastBackslash - Buffer;
    NewString = HeapAlloc(GetProcessHeap(), 0, ((DirLength + 1) * sizeof(TCHAR)));
    lstrcpyn(NewString, Buffer, (DirLength + 1)); /* lstrcpyn include null character */
  }
  else
  {
    NewString = NULL;
  }

  return NewString;
}

static void PS_ReportExecutionError (int ErrorCode)
{
  DWORD BytesWritten;
  
  DWORD_PTR Args[] = {
    (DWORD_PTR)ErrorCode
  };

  BytesWritten = FormatMessage(FORMAT_MESSAGE_FROM_STRING
                               | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                               _T("The child process terminated abnormally.\n")
                               _T("Return code %1!d! (0x%1!x!)"),
                               0,
                               0,
                               (LPWSTR)PS_BufferOut,
                               sizeof(PS_BufferOut),
                               (char **)Args);

  if (BytesWritten > 0)
  {
    PS_MessageAndExit(5, PS_BufferOut, MB_ICONERROR);
  }
  else
  {
    PS_MessageAndExit(6, PS_UNEXPECTED_ERROR, MB_ICONERROR);
  }
}

static int PS_RunProcess (TCHAR *CommandLine)
{
  BOOL                 CpResult;
  BOOL                 ExitCodeSuccess;
  STARTUPINFO          si;
  PROCESS_INFORMATION  pi;
  int                  CreateFlags;
  DWORD                ExitCode;
  DWORD                BytesWritten;

  SecureZeroMemory(&si, sizeof(si));
  SecureZeroMemory(&pi, sizeof(pi));

  si.cb = sizeof(si);

  if (PS_OPTION_INIT_COMMON_CONTROLS == TRUE)
  {
    InitCommonControls();
  }

  if (PS_OPTION_SHOW_CONSOLE == TRUE)
  {
    CreateFlags = 0;
  }
  else
  {
    CreateFlags = CREATE_NO_WINDOW;
  }

  if (PS_OPTION_DEBUG == TRUE)
  {
    MessageBox(NULL, CommandLine, _T("DEBUG"), MB_ICONINFORMATION);
  }

  CpResult = CreateProcess(NULL,        /* NULL: use command line        */
                           CommandLine, /* Command line                  */
                           NULL,        /* Process handle not inheritable*/
                           NULL,        /* Thread handle not inheritable */
                           FALSE,       /* No handle inheritance         */
                           CreateFlags, /* Creation flags                */
                           NULL,        /* Use parent environment block  */
                           NULL,        /* Use parent starting directory */
                           &si,         /* STARTUPINFO structure         */
                           &pi);        /* PROCESS_INFORMATION structure */

  if (CpResult == TRUE)
  {
    if ((PS_OPTION_MONITOR_PROCESS == TRUE) || (PS_OPTION_DEBUG == TRUE))
    {
      /* Wait until child process exits */
      WaitForSingleObject(pi.hProcess, INFINITE);

      /* Retrieve the exit code */
      ExitCodeSuccess = GetExitCodeProcess(pi.hProcess, &ExitCode);
      
      /* Retrieve the exit code */
      if (ExitCodeSuccess == TRUE)
      {
        if (PS_OPTION_DEBUG == TRUE)
        {
          PS_ReportExecutionError(ExitCode);
        }
      }
      else
      {
        ExitCode = 99999;
      }
    }
    else
    {
      ExitCode = 0;
    }

    /* Close process and thread handles */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  else
  {
    ExitCode = -1;
    if (GetEnvironmentVariable(_T("PATH"), PS_BufferIn, (sizeof(PS_BufferIn))) == 0)
    {
      PS_BufferIn[0] = _T('\0');
    }

    DWORD_PTR Args[] = {
      (DWORD_PTR)CommandLine,
      (DWORD_PTR)PS_BufferIn
    };

    BytesWritten = FormatMessage(FORMAT_MESSAGE_FROM_STRING
                                 | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                 _T("Error: the command line could not be executed\n")
                                 _T("[%1!s!]\n\n")
                                 _T("PATH: '%2!s!'\n"),
                                 0,
                                 0,
                                 (LPWSTR)&PS_BufferIn,
                                 sizeof(PS_BufferIn),
                                 (char **)Args);
    if (BytesWritten > 0)
    {
      MessageBox(NULL, PS_BufferIn, _T("Error#08"), MB_ICONERROR);
    }
    else
    {
      MessageBox(NULL, PS_UNEXPECTED_ERROR, _T("Error#09"), MB_ICONERROR);
    }
  }

  return ExitCode;
}

static void PS_SM_Initialize (const TCHAR *ProgramDirectory)
{
  /* Set a temporary environment variable with the location of the binary file,
   * so that it is straight-forward to expand the command line arguments with
   * ExpandEnvironmentStrings
   */
  SetEnvironmentVariable(_T("PLAINSTARTER_DIRECTORY"), ProgramDirectory);
}

static BOOL PS_SM_HasOption (TCHAR *Options, TCHAR *Option)
{
  BOOL Result;

  if (StrStr(Options, Option) == NULL)
  {
    Result = FALSE;
  }
  else
  {
    Result = TRUE;
  }

  return Result;
}

static void PS_SM_ReadOptions ()
{
  /* Put the options in BufferIn */
  ExpandEnvironmentStrings(L"%PLAINSTARTER_OPTIONS%", PS_BufferIn, sizeof(PS_BufferIn));

  PS_OPTION_SHOW_CONSOLE         = PS_SM_HasOption(PS_BufferIn, _T("show-console"));
  PS_OPTION_INIT_COMMON_CONTROLS = PS_SM_HasOption(PS_BufferIn, _T("init-common-controls"));
  PS_OPTION_MONITOR_PROCESS      = PS_SM_HasOption(PS_BufferIn, _T("monitor-process"));
  PS_OPTION_DEBUG                = PS_SM_HasOption(PS_BufferIn, _T("debug"));
}

static void PS_SM_ProcessVariable (const TCHAR *Name,
                                   const TCHAR *Value,
                                   int          argc,
                                   TCHAR      **argv)
{
  const TCHAR *CMD_LINE = _T("PLAINSTARTER_CMD_LINE");
  TCHAR *p;
  int    i;

  if (lstrcmp(Name, CMD_LINE) == 0)
  {
    p = PS_BufferIn + lstrlen(PS_BufferIn);
    for (i=1 ; i<argc ; i++)
    {
      *p++ = _T(' ');
      *p++ = _T('\"');
      p = PS_StringAppend(p, argv[i], (argv[i] + lstrlen(argv[i]) - 1));
      *p++ = _T('\"');
    }

    /* End the string */
    *p = _T('\0');

    if (ExpandEnvironmentStrings(PS_BufferIn, PS_BufferOut, sizeof(PS_BufferOut)) == 0)
    {
      /* If fails, just copy the buffer unchanged */
      p = PS_StringAppend(PS_BufferOut, PS_BufferIn, PS_BufferIn + lstrlen(PS_BufferIn));
      p[0] = _T('\0');
    }

    PS_SM_ReadOptions();

    /* The expansion is done, delete useless environment variables */
    SetEnvironmentVariable(_T("PLAINSTARTER_CMD_LINE"),  NULL);
    SetEnvironmentVariable(_T("PLAINSTARTER_PROGNAME"),  NULL);
    SetEnvironmentVariable(_T("PLAINSTARTER_DIRECTORY"), NULL);
    SetEnvironmentVariable(_T("PLAINSTARTER_OPTIONS"),   NULL);

    /* Run the process */
    PS_LAST_EXEC_CODE = PS_RunProcess(PS_BufferOut);
  }
  else
  {
    /* Try to expand the references to environment variables (ie %PATH%) */
    if (ExpandEnvironmentStrings(Value, PS_BufferOut, sizeof(PS_BufferOut)) != 0)
    {
      Value = PS_BufferOut;
    }
    else
    {
      PS_MessageAndExit(7, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
    }

    /* Set the environment variable */
    if (SetEnvironmentVariable(Name, Value) == 0)
    {
      PS_MessageAndExit(8, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
    }
  }
}

static void PS_ParseConfiguration (TCHAR  *Buffer,
                                   TCHAR  *BinaryDirectory,
                                   int     argc,
                                   TCHAR **argv)
{
  TCHAR  VariableName[PS_MAX_LINE_LEN_BYTES];
  TCHAR *VariableValue = PS_BufferIn;
  TCHAR *p;

  enum { STATE_ReadName, STATE_ReadValue, STATE_ReadComment, STATE_ReadN } ParserState;
  unsigned int Index;

  int Flags = IS_TEXT_UNICODE_SIGNATURE | IS_TEXT_UNICODE_REVERSE_SIGNATURE;

  /* Parse the Buffer */
  ParserState = STATE_ReadName;
  Index       = 0;
  p           = Buffer;

  PS_SM_Initialize(BinaryDirectory);

  /* Skip Unicode UTF-16 BOM */
  if (IsTextUnicode(Buffer, 4, &Flags) > 0)
  {
    p++;
  }
  else
  {
    PS_MessageAndExit(9, _T("Expecting UTF-16 encoded configuration file."), EXIT_FAILURE);
  }

  while (*p)
  {
    if (*p == _T('#'))
    {
      ParserState = STATE_ReadComment;
    }

    /* Unexpected new line, skip silently */
    if ((ParserState != STATE_ReadValue)
        && (*p == _T('\r')))
    {
      ParserState = STATE_ReadN;
    }

    if (Index > PS_MAX_LINE_LEN_BYTES)
    {
      ParserState = STATE_ReadN;
    }

    switch (ParserState)
    {
    case STATE_ReadComment:
      if (*p == _T('\r'))
      {
        ParserState = STATE_ReadN;
      }
      break;

    case STATE_ReadName:
      if (*p == _T('='))
      {
        VariableName[Index] = _T('\0');
        ParserState = STATE_ReadValue;
        Index = 0;
      }
      else
      {
        VariableName[Index] = *p;
        Index++;
      }
      break;

    case STATE_ReadValue:
      if (*p == _T('\r'))
      {
        VariableValue[Index] = _T('\0');
        ParserState = STATE_ReadN;
        PS_SM_ProcessVariable(VariableName, VariableValue, argc, argv);
      }
      else
      {
        VariableValue[Index] = *p;
        Index++;
      }
      break;

    case STATE_ReadN:
      if (*p == _T('\n'))
      {
        ParserState = STATE_ReadName;
        Index = 0;
      }
      break;
    }

    /* process next character */
    p++;
  }
}

static int PS_EffectiveMain (int argc, TCHAR **argv)
{
  HANDLE HeapHandle       = GetProcessHeap();
  TCHAR *Argv0            = argv[0];
  TCHAR *ConfigFilename   = NULL;
  TCHAR *ConfigData       = NULL;
  TCHAR *ProgramDirectory = NULL;

  PS_LAST_EXEC_CODE = EXIT_SUCCESS;
  ConfigFilename = PS_GetConfigFilename(PS_CONFIG_DIR_1, PS_ARRAY_SIZE(PS_CONFIG_DIR_1), Argv0);
  ConfigData     = PS_FileSlurp(ConfigFilename);

  if (ConfigData == NULL)
  {
    HeapFree(HeapHandle, 0, ConfigFilename);
    ConfigFilename = PS_GetConfigFilename(PS_CONFIG_DIR_2, PS_ARRAY_SIZE(PS_CONFIG_DIR_2), Argv0);
    ConfigData     = PS_FileSlurp(ConfigFilename);
  }

  if (ConfigData == NULL)
  {
    HeapFree(HeapHandle, 0, ConfigFilename);
    ConfigFilename = PS_GetConfigFilename(PS_CONFIG_DIR_3, PS_ARRAY_SIZE(PS_CONFIG_DIR_3), Argv0);
    ConfigData     = PS_FileSlurp(ConfigFilename);
  }

  if (ConfigData == NULL)
  {
    PS_MessageAndExit(10, _T("Configuration file not found."), EXIT_FAILURE);
  }
  else
  {
    ProgramDirectory = PS_LocateWin32BinaryDirectory(PS_BufferIn, sizeof(PS_BufferIn));
    PS_ParseConfiguration(ConfigData, ProgramDirectory, argc, argv);
    HeapFree(HeapHandle, 0, ProgramDirectory);
    HeapFree(HeapHandle, 0, ConfigData);
  }

  /* Free resources */
  HeapFree(HeapHandle, 0, ConfigFilename);

  return PS_LAST_EXEC_CODE;
}

/*---------------*/
/* MAIN FUNCTION */
/*---------------*/

/**
 * Entry point is defined as follow:
 *   - Console  application: int WINAPI mainCRTStartup    (void)
 *   - Windows  application: int WINAPI WinMainCRTStartup (void)
 *   - Standard application: int main (int, char **)
 *
 * MinGW does not seems to generate any macro to determine the selected
 * subsystem. For this reason, the build script define the macro
 * PLAINSTARTER_CONSOLE and PLAINSTARTER_WINDOWS.
 *
 * Standard application is used to be able use GDB for debugging.
 *
 * This can be checked with the following command:
 *   - gcc -dM -E src\plainstarter-win32.c > plainstarter-defines.h
 *
 */
#ifdef PLAINSTARTER_CONSOLE
int WINAPI mainCRTStartup (void)
#elif PLAINSTARTER_WINDOWS
  int WINAPI WinMainCRTStartup (void)
#else
  int main (int argc, char **argv)
#endif
{
#if defined(PLAINSTARTER_CONSOLE) || defined(PLAINSTARTER_WINDOWS)
  int     ReturnCode;
  LPWSTR *ArgList;
  int     ArgCount;

  ArgList = CommandLineToArgvW(GetCommandLineW(), &ArgCount);
  if (ArgList == NULL)
  {
    PS_MessageAndExit(11, PS_UNEXPECTED_ERROR, EXIT_FAILURE);
  }
  else
  {
    ReturnCode = PS_EffectiveMain(ArgCount, ArgList);
    LocalFree(ArgList);
  }

  /* WIN32 API requires to return with ExitProcess instead of using 'return'
   * keyword. Using 'return' keyword will lead to the corruption of the stack
   * and the crash of the application. */
  ExitProcess(ReturnCode);
#else
  PS_EffectiveMain(argc, argv);
#endif
}
