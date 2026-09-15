/*
 * Local compatibility header — WESTLAKE.
 *
 * android_net_NetworkUtils.cpp (modules-connectivity/framework/jni) includes
 * <DnsProxydProtocol.h> for exactly one symbol, NETID_USE_LOCAL_NAMESERVERS.
 * That header lives in packages/modules/DnsResolver, which is NOT part of the
 * pinned android-15.0.0_r1 source set vendored under android-source/, so the
 * core framework-connectivity JNI could not be compiled at all and
 * NetworkUtils.<clinit> died with UnsatisfiedLinkError.
 *
 * The value is a wire-protocol flag shared by DnsProxyListener.cpp and
 * NetdClient.cpp and is explicitly documented upstream as "should not be
 * changed", so reproducing the single constant is exact rather than a guess.
 *
 * Upstream definition (Apache-2.0, The Android Open Source Project):
 *   packages/modules/DnsResolver/include/dnsproxyd_protocol/DnsProxydProtocol.h
 *
 * This is a compatibility input, not a regenerated upstream artifact. The
 * proper closure is to vendor the matching DnsResolver module into
 * android-source/ and drop this file; see DEPENDENCIES.md.
 */

#pragma once

/*
 * This value should not be changed.
 * It's a flag used in both DnsProxyListener.cpp and NetdClient.cpp
 * to identify if bypassing DoT is available.
 * This flag must be kept in sync with the Network#getNetIdForResolv() usage.
 */
#define NETID_USE_LOCAL_NAMESERVERS 0x80000000
