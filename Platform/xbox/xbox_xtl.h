/* xbox_xtl.h -- <xtl.h> for the port's C files.
 *
 * d3dx8math.inl defines its helpers `inline` without `static`; in C that
 * makes every translation unit including <xtl.h> emit the same external
 * symbols, and the second such file fails the link. Defining the .inl's own
 * include guard first skips it: nothing here uses the D3DX maths. */
#ifndef DKRX_XBOX_XTL_H
#define DKRX_XBOX_XTL_H

#ifndef __cplusplus
#define __D3DX8MATH_INL__
#endif
#include <xtl.h>

#endif /* DKRX_XBOX_XTL_H */
