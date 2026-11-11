#!/usr/bin/env python3
"""Generate framework/core/java/AccountManagerStub.java from AOSP's IAccountManager.aidl.

usage: tools/gen_account_manager_stub.py <IAccountManager.aidl> <AccountManagerStub.java>

Every IAccountManager method becomes a real Stub method that hands its name and arguments to
LocalAccounts.answer, as the in-process proxy did, so the account service can also answer a caller in
another process (the Play Store host, through SocketBinder). Rerun it when the AIDL changes.
"""
import re
import sys

IMPORTS = {
    'Account': 'android.accounts.Account',
    'AuthenticatorDescription': 'android.accounts.AuthenticatorDescription',
    'IAccountManagerResponse': 'android.accounts.IAccountManagerResponse',
    'Bundle': 'android.os.Bundle',
    'UserHandle': 'android.os.UserHandle',
    'IntentSender': 'android.content.IntentSender',
    'Map': 'java.util.Map',
}
BOXED = {'boolean': 'Boolean', 'int': 'Integer', 'long': 'Long'}


def methods(aidl):
    text = re.sub(r'/\*.*?\*/', '', aidl, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    body = text[text.index('{', text.index('interface')) + 1:text.rindex('}')]
    for declaration in body.split(';'):
        declaration = re.sub(r'@\w+(\([^)]*\))?', '', declaration).strip()
        declaration = re.sub(r'\boneway\s+', '', declaration)
        if not declaration:
            continue
        match = re.fullmatch(r'([\w.\[\]]+)\s+(\w+)\s*\((.*)\)\s*(=\s*\d+)?', declaration, re.S)
        if not match:
            raise ValueError('cannot parse: ' + declaration)
        returns, name, params = match[1], match[2], match[3].strip()
        parsed = []
        for param in filter(None, (p.strip() for p in params.split(','))):
            words = [w for w in param.split() if w not in ('in', 'out', 'inout')]
            parsed.append((words[0], words[1]))
        yield returns, name, parsed


def java(returns, name, params):
    signature = ', '.join(t + ' ' + n for t, n in params)
    # An explicit array, so that a lone array argument is never spread as varargs.
    args = 'new Object[] {%s}' % ', '.join(n for _, n in params)
    call = 'call("%s", %s.class, %s)' % (name, returns, args)
    if returns == 'void':
        body = call + ';'
    elif returns in BOXED:
        body = 'return (%s) %s;' % (BOXED[returns], call)
    else:
        body = 'return (%s) %s;' % (returns, call)
    return ('    @Override\n    public %s %s(%s) {\n        %s\n    }\n' % (returns, name, signature, body))


def main():
    source, target = sys.argv[1], sys.argv[2]
    found = list(methods(open(source).read()))
    used = sorted({t.rstrip('[]') for r, _, ps in found for t in [r] + [p[0] for p in ps]} & IMPORTS.keys())
    out = ['package adapter.core;\n\n']
    imports = sorted([IMPORTS[t] for t in used] + ['android.accounts.IAccountManager'])
    out += ['import %s;\n' % name for name in imports]
    out.append('''
/**
 * The account service as a real IAccountManager.Stub, answered by LocalAccounts: generated from AOSP's
 * IAccountManager.aidl by tools/gen_account_manager_stub.py, do not edit by hand.
 *
 * A Stub serves callers in other processes as well as this one. The Play Store host's account
 * manager is this service in the Play services host, reached through SocketBinder, where
 * Binder.getCallingUid is the Play Store's own uid; the in-process proxy it replaces answered only
 * calls made in this process.
 */
final class AccountManagerStub extends IAccountManager.Stub {
    private static final java.util.Set<String> SEEN =
            java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());

    /** The uid and pid of the process whose transaction this thread is serving, or null in a direct call. */
    private static final ThreadLocal<int[]> REMOTE = new ThreadLocal<>();

    /**
     * Who is calling, as AccountManagerService sees it: the other process during a transaction that came
     * through this Stub, and this process for a direct call. A direct call made on a thread that is
     * serving some app's request (Play services' code on a SocketBinder thread) is still this process's
     * own call, as it would be a call from Play services into system_server on Android, whatever
     * Binder.getCallingUid says on that thread.
     */
    static int callerUid() {
        int[] remote = REMOTE.get();
        return remote != null ? remote[0] : android.os.Process.myUid();
    }

    static int callerPid() {
        int[] remote = REMOTE.get();
        return remote != null ? remote[1] : android.os.Process.myPid();
    }

    @Override
    public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags)
            throws android.os.RemoteException {
        int[] outer = REMOTE.get();
        REMOTE.set(new int[] {android.os.Binder.getCallingUid(), android.os.Binder.getCallingPid()});
        try {
            return super.onTransact(code, data, reply, flags);
        } finally {
            REMOTE.set(outer);
        }
    }

    /** One call: logged the first time, as LocalServiceBinders' proxies are; DEFAULT answers as they do. */
    private static Object call(String method, Class<?> returns, Object[] args) {
        if (SEEN.add(method)) System.err.println("[WESTLAKE-LOCAL-SERVICE] account." + method);
        Object value = LocalAccounts.answer(method, args);
        return value == LocalServiceBinders.DEFAULT ? LocalServiceBinders.defaultValue(returns) : value;
    }

''')
    out += [java(*m) + '\n' for m in found]
    out[-1] = out[-1].rstrip('\n') + '\n'
    out.append('}\n')
    open(target, 'w').write(''.join(out))
    print('%d methods -> %s' % (len(found), target))


if __name__ == '__main__':
    main()
