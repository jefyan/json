// Copyright (c) 2018-2019 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#ifndef TAO_JSON_PEGTL_INTERNAL_PEEK_UINT8_HPP
#define TAO_JSON_PEGTL_INTERNAL_PEEK_UINT8_HPP

#include <cstddef>
#include <cstdint>

#include "../config.hpp"

#include "input_pair.hpp"

namespace TAO_JSON_PEGTL_NAMESPACE::internal
{
   struct peek_uint8
   {
      using data_t = std::uint8_t;
      using pair_t = input_pair< std::uint8_t >;

      template< typename Input >
      [[nodiscard]] static pair_t peek( Input& in, const std::size_t o = 0 ) noexcept( noexcept( in.Input::peek_uint8( 0 ) ) )
      {
         return { in.peek_uint8( o ), 1 };
      }
   };

}  // namespace TAO_JSON_PEGTL_NAMESPACE::internal

#endif
