#ifndef LUNA_TEST_LUNA_C_API_HPP
#define LUNA_TEST_LUNA_C_API_HPP

extern "C" {
#include "luna/backend/x86_64/x86_64.h"
#include "luna/frontend/ast/ast.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/lexer/lexer.h"
#include "luna/frontend/parser/parser.h"
#include "luna/frontend/source/source.h"
#include "luna/frontend/support/arena.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
#include "luna/frontend/token/token.h"
#include "luna/middleend/ir/ir.h"
#include "luna/middleend/sema/sema.h"
#include "luna/target/target.h"
}

#endif
