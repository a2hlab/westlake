// Host-only white-box diagnostic. Never linked into the keyboard.
#include <cstdio>
#include <cstdlib>
#define private public
#define protected public
#include "matrixsearch.h"
#include "userdict.h"
#undef private
#undef protected
#include "pinyinime.h"
extern "C" ime_pinyin::MatrixSearch* matrix_search;
extern "C" void inspect_learning() {
    auto* m=matrix_search;
    std::printf("user=%p fixed=%zu lemmas=%zu\n",static_cast<void*>(m->user_dict_),m->fixed_lmas_,m->lma_id_num_);
    for(unsigned i=0;i<m->lma_id_num_;i++)std::printf(" lma=%zu first=%u\n",m->lma_id_[i],m->fixed_lmas_no1_[i]);
    auto* u=static_cast<ime_pinyin::UserDict*>(m->user_dict_);
    if(u)std::printf(" state=%u learned=%u\n",u->state_,u->dict_info_.lemma_count);
}
