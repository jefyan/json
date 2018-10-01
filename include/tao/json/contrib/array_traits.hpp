// Copyright (c) 2018 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAO_JSON_CONTRIB_ARRAY_TRAITS_HPP
#define TAO_JSON_CONTRIB_ARRAY_TRAITS_HPP

#include <array>

#include "../consume.hpp"
#include "../forward.hpp"

#include "internal/array_traits.hpp"

namespace tao
{
   namespace json
   {
      template< typename T, std::size_t N >
      struct array_traits
         : public internal::array_traits< std::array< T, N > >
      {
         template< template< typename... > class Traits, typename Base >
         static void to( const basic_value< Traits, Base >& v, std::array< T, N >& r )
         {
            const auto& a = v.get_array();
            for( std::size_t i = 0; i < N; ++i ) {
               v.to( r[ i ] );
            }
         }

         template< template< typename... > class Traits, typename Producer >
         static void consume( Producer& parser, std::array< T, N >& r )
         {
            auto s = parser.begin_array();
            for( std::size_t i = 0; i < N; ++i ) {
               parser.element( s );
               json::consume< Traits >( parser, r[ i ] );
            }
            parser.end_array( s );
         }
      };

   }  // namespace json

}  // namespace tao

#endif