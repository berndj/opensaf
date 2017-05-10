#ifndef LCK_LCKND_GLND_CLM_H_
#define LCK_LCKND_GLND_CLM_H_

#include "lck/lcknd/glnd.h"

#ifdef  __cplusplus
extern "C" {
#endif

SaAisErrorT glnd_clm_init(GLND_CB *);
SaAisErrorT glnd_clm_deinit(GLND_CB *);

#ifdef  __cplusplus
}
#endif

#endif  // LCK_LCKND_GLND_CLM_H_
