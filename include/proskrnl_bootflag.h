/*
 * proskrnl_bootflag.h — the one PE-side reader of a proskrnl boot flag.
 *
 * PROSKRNL-ONLY, and dormant under Wine by construction: off a proskrnl guest
 * the key below does not exist, so every caller gets its own `whenNotQemu`
 * default and takes exactly the path it took before this header existed.
 * `run.sh oracle` runs these bytes on real Wine every time, which is the
 * standing proof of that (docs/06).
 *
 * proskrnl's kernel publishes what the QEMU command line carried as REG_DWORD
 * values under the volatile \Registry\Machine\Hardware\qemu (HACK-006), and
 * its session manager publishes the values it DERIVES from them beside those.
 * Four PE modules ask that key a question each — conhost "ConsoleWindow",
 * winefb.drv and wineserver-lite "ShellBoot", win32u "Gui" — and each had
 * grown its own copy of the same twenty lines.  Copies of a rule drift even
 * while they still agree.
 *
 * It lives in the WINE tree rather than proskrnl's because win32u asks too,
 * and the Wine tree has to build standalone: the oracle compiles it with
 * Wine's own configure/make, which knows nothing of proskrnl's include paths.
 * proskrnl's own modules reach it through -Ithird_party/wine/include, which
 * their build lines already carry.
 *
 * The one thing that legitimately differs between askers is the answer when
 * there is NO `qemu` key — not a proskrnl guest at all, nothing said either
 * way, and each flag's own default applies.  That is the `whenAbsent`
 * parameter, so a difference between two callers is a value at the call site
 * rather than a divergence buried in a copied body.  Every OTHER failure —
 * the key exists but the value is absent, or is not a REG_DWORD, or is the
 * wrong size — is the guest having decided, so it reads as 0 for everyone.
 *
 * Nothing here is an NT contract.  Requires winternl.h/winnt.h
 * (UNICODE_STRING, OBJECT_ATTRIBUTES, NtOpenKey, NtQueryValueKey, REG_DWORD).
 */
#ifndef PRSK_BOOTFLAG_H
#define PRSK_BOOTFLAG_H

static inline ULONG prsk_qemu_boot_flag( const WCHAR *value_name, ULONG when_absent )
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attr;
    HANDLE key;
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION info;
        UCHAR tail[sizeof(ULONG)];
    } buffer;
    ULONG result_length = 0, value = 0;

    RtlInitUnicodeString( &name, L"\\Registry\\Machine\\Hardware\\qemu" );
    InitializeObjectAttributes( &attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL );
    if (NtOpenKey( &key, KEY_QUERY_VALUE, &attr )) return when_absent;

    RtlInitUnicodeString( &name, (WCHAR *)value_name );
    if (!NtQueryValueKey( key, &name, KeyValuePartialInformation, &buffer, sizeof(buffer),
                          &result_length ) &&
        buffer.info.Type == REG_DWORD && buffer.info.DataLength == sizeof(ULONG))
        memcpy( &value, buffer.info.Data, sizeof(value) );
    NtClose( key );
    return value;
}

/* Does this boot have a DESKTOP at all?  Cached: it cannot change within a
 * boot, and win32u asks on a path taken per thread.  The race between two
 * first-callers is benign — they compute the same answer. */
static inline int prsk_boot_has_desktop( void )
{
    static int cached = -1;
    if (cached < 0) cached = prsk_qemu_boot_flag( L"Gui", 1 ) != 0;
    return cached;
}

#endif /* PRSK_BOOTFLAG_H */
