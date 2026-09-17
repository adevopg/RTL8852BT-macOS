/* SPDX-License-Identifier: BSD-3-Clause
 *
 * kmod_info.cpp - punto de entrada del modulo de kernel
 *
 * Xcode genera esto solo a partir de los ajustes MODULE_NAME y MODULE_VERSION.
 * Como aqui compilamos a mano con clang y un Makefile, hay que escribirlo.
 *
 * Sin este fichero el kext compila y enlaza, pero `kextload` lo rechaza: le
 * faltan los simbolos `_kmod_info`, `__realmain` y `__antimain`, que son lo
 * que el kernel busca para arrancar y parar el modulo.
 *
 * Lo detecto el paso "simbolos ajenos al kernel" del workflow de CI.
 *
 * Ojo: la cadena de KMOD_EXPLICIT_DECL tiene que coincidir EXACTAMENTE con
 * CFBundleIdentifier y CFBundleVersion de Info.plist, o el kext no carga.
 */
#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>
#include <libkern/libkern.h>

extern "C" {

kern_return_t RTL8852BT_kmod_start(kmod_info_t *ki, void *d);
kern_return_t RTL8852BT_kmod_stop(kmod_info_t *ki, void *d);

/* Debe coincidir con Info.plist:
 *   CFBundleIdentifier  = com.poveda.driver.RTL8852BT
 *   CFBundleVersion     = 0.1.0
 * KMOD_EXPLICIT_DECL no lleva comillas en el identificador, es intencionado. */
KMOD_EXPLICIT_DECL(com.poveda.driver.RTL8852BT, "0.1.0", _start, _stop)

/* libkmodc++ define _start/_stop; estos llaman a los constructores estaticos
 * de C++ y luego saltan a _realmain / _antimain. */
__private_extern__ kmod_start_func_t *_realmain = RTL8852BT_kmod_start;
__private_extern__ kmod_stop_func_t  *_antimain = RTL8852BT_kmod_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;

/* El trabajo real lo hace IOKit al emparejar RTL8852BT con el IOPCIDevice,
 * asi que aqui no hay nada que hacer mas que decir que todo fue bien. */
kern_return_t RTL8852BT_kmod_start(kmod_info_t *ki, void *d)
{
	(void)ki; (void)d;
	return KERN_SUCCESS;
}

kern_return_t RTL8852BT_kmod_stop(kmod_info_t *ki, void *d)
{
	(void)ki; (void)d;
	return KERN_SUCCESS;
}

} /* extern "C" */
