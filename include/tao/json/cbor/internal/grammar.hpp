// Copyright (c) 2017-2018 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAOCPP_INCLUDE_JSON_CBOR_INTERNAL_GRAMMAR_HPP
#define TAOCPP_INCLUDE_JSON_CBOR_INTERNAL_GRAMMAR_HPP

#include <cmath>
#include <cstdint>
#include <utility>

#include "major.hpp"

#include "../../byte_view.hpp"
#include "../../external/byte.hpp"
#include "../../external/pegtl.hpp"
#include "../../external/string_view.hpp"
#include "../../internal/endian.hpp"
#include "../../utf8.hpp"

namespace tao
{
   namespace json
   {
      namespace cbor
      {
         namespace internal
         {
            template< typename Input >
            void throw_on_empty( Input& in, const std::size_t size = 1 )
            {
               if( in.size( size ) < size ) {
                  throw json_pegtl::parse_error( "unexpected end of cbor input", in );
               }
            }

            template< typename Input >
            major peek_major( Input& in )
            {
               return static_cast< major >( in.peek_byte() & major_mask );
            }

            template< typename Input >
            std::uint8_t peek_minor( Input& in )
            {
               return in.peek_byte() & minor_mask;
            }

            template< typename Input >
            std::uint8_t peek_byte_safe( Input& in )
            {
               throw_on_empty( in );
               return in.peek_byte();
            }

            template< typename Input >
            major peek_major_safe( Input& in )
            {
               return static_cast< major >( peek_byte_safe( in ) & major_mask );
            }

            template< utf8_mode V >
            struct data
            {
               using analyze_t = json_pegtl::analysis::generic< json_pegtl::analysis::rule_type::ANY >;

               template< json_pegtl::apply_mode A,
                         json_pegtl::rewind_mode M,
                         template< typename... > class Action,
                         template< typename... > class Control,
                         typename Input,
                         typename Consumer >
               static bool match( Input& in, Consumer& consumer )
               {
                  // This rule never returns false unless the input is empty.
                  return ( !in.empty() ) && match_impl( in, consumer );
               }

               template< typename Input, typename Consumer >
               static bool match_impl( Input& in, Consumer& consumer )
               {
                  switch( internal::peek_major( in ) ) {
                     case major::UNSIGNED:
                        return match_unsigned( in, consumer );
                     case major::NEGATIVE:
                        return match_negative( in, consumer );
                     case major::BINARY:
                        return match_binary( in, consumer );
                     case major::STRING:
                        return match_string( in, consumer );
                     case major::ARRAY:
                        return match_array( in, consumer );
                     case major::OBJECT:
                        return match_object( in, consumer );
                     case major::TAG:
                        return match_tag( in, consumer );
                     case major::OTHER:
                        return match_other( in, consumer );
                  }
                  // LCOV_EXCL_START
                  assert( false );
                  return false;
                  // LCOV_EXCL_STOP
               }

               template< typename Input >
               static void skip_unsigned( Input& in )
               {
                  // Assumes in.size( 1 ) >= 1 and in.peek_byte() is the byte with major/minor.

                  switch( internal::peek_minor( in ) ) {
                     default:
                        in.bump_in_this_line();
                        return;
                     case 24:
                        in.bump_in_this_line( 2 );
                        return;
                     case 25:
                        in.bump_in_this_line( 3 );
                        return;
                     case 26:
                        in.bump_in_this_line( 5 );
                        return;
                     case 27:
                        in.bump_in_this_line( 9 );
                        return;
                     case 28:
                     case 29:
                     case 30:
                     case 31:
                        throw json_pegtl::parse_error( "unexpected minor for number or length", in );
                  }
               }

               template< typename Input >
               static std::uint64_t read_embedded_impl( Input& in )
               {
                  const auto result = internal::peek_minor( in ) & minor_mask;
                  in.bump_in_this_line();
                  return result;
               }

               template< typename Unsigned, typename Input >
               static std::uint64_t read_unsigned_impl( Input& in )
               {
                  if( 1 + in.size( sizeof( Unsigned ) ) > sizeof( Unsigned ) ) {
                     const Unsigned result = json::internal::be_to_h< Unsigned >( in.current() + 1 );
                     in.bump_in_this_line( 1 + sizeof( Unsigned ) );
                     return result;
                  }
                  throw json_pegtl::parse_error( "unexpected end of input", in );
               }

               template< typename Input >
               static std::uint64_t read_unsigned( Input& in )
               {
                  // Assumes in.size( 1 ) >= 1 and in.peek_byte() is the byte with major/minor.

                  switch( internal::peek_minor( in ) ) {
                     default:
                        return read_embedded_impl( in );
                     case 24:
                        return read_unsigned_impl< std::uint8_t >( in );
                     case 25:
                        return read_unsigned_impl< std::uint16_t >( in );
                     case 26:
                        return read_unsigned_impl< std::uint32_t >( in );
                     case 27:
                        return read_unsigned_impl< std::uint64_t >( in );
                     case 28:
                     case 29:
                     case 30:
                     case 31:
                        throw json_pegtl::parse_error( "unexpected minor for number or length", in );
                  }
               }

               template< typename Input >
               static std::size_t read_size( Input& in )
               {
                  const auto s = read_unsigned( in );
                  if( s > static_cast< std::uint64_t >( std::numeric_limits< std::size_t >::max() ) ) {
                     throw json_pegtl::parse_error( "cbor size exceeds size_t " + std::to_string( s ), in );
                  }
                  return static_cast< std::size_t >( s );
               }

               template< typename Input, typename Consumer >
               static bool match_unsigned( Input& in, Consumer& consumer )
               {
                  consumer.number( read_unsigned( in ) );
                  return true;
               }

               template< typename Input, typename Consumer >
               static bool match_negative( Input& in, Consumer& consumer )
               {
                  const auto u = read_unsigned( in );
                  if( u > 9223372036854775808ull ) {
                     throw json_pegtl::parse_error( "negative integer overflow", in );
                  }
                  consumer.number( std::int64_t( ~u ) );
                  return true;
               }

               // "String" is text string or byte string in RFC 7049 terminology.

               template< utf8_mode U, typename Result, typename Input >
               static Result read_string_1( Input& in )
               {
                  using value_t = typename Result::value_type;
                  const auto size = read_size( in );
                  internal::throw_on_empty( in, size );
                  const auto* pointer = static_cast< const value_t* >( static_cast< const void* >( in.current() ) );
                  Result result( pointer, size );
                  json::internal::consume_utf8< U >( in, size );
                  return result;
               }

               template< utf8_mode U, typename Result, typename Input >
               static Result read_string_n( Input& in, const major m )
               {
                  using value_t = typename Result::value_type;

                  Result result;
                  in.bump_in_this_line();
                  while( internal::peek_byte_safe( in ) != 0xff ) {
                     if( internal::peek_major( in ) != m ) {
                        throw json_pegtl::parse_error( "non-matching fragment in indefinite length string", in );
                     }
                     const auto size = read_size( in );
                     internal::throw_on_empty( in, size );
                     const auto* pointer = static_cast< const value_t* >( static_cast< const void* >( in.current() ) );
                     result.insert( result.end(), pointer, pointer + size );
                     json::internal::consume_utf8< U >( in, size );
                  }
                  in.bump_in_this_line();
                  return result;
               }

               template< typename Input, typename Consumer >
               static bool match_string( Input& in, Consumer& consumer )
               {
                  // Assumes in.size( 1 ) >= 1 and in.peek_byte() is the byte with major/minor.

                  if( internal::peek_minor( in ) != minor_mask ) {
                     consumer.string( read_string_1< V, tao::string_view >( in ) );
                  }
                  else {
                     consumer.string( read_string_n< V, std::string >( in, major::STRING ) );
                  }
                  return true;
               }

               template< typename Input, typename Consumer >
               static bool match_binary( Input& in, Consumer& consumer )
               {
                  // Assumes in.size( 1 ) >= 1 and in.peek_byte() is the byte with major/minor.

                  if( internal::peek_minor( in ) != minor_mask ) {
                     consumer.binary( read_string_1< utf8_mode::TRUST, tao::byte_view >( in ) );
                  }
                  else {
                     consumer.binary( read_string_n< utf8_mode::TRUST, std::vector< tao::byte > >( in, major::BINARY ) );
                  }
                  return true;
               }

               template< typename Input, typename Consumer >
               static void match_array_1( Input& in, Consumer& consumer )
               {
                  const auto size = read_size( in );
                  consumer.begin_array( size );
                  for( std::size_t i = 0; i < size; ++i ) {
                     internal::throw_on_empty( in );
                     match_impl( in, consumer );
                     consumer.element();
                  }
                  consumer.end_array( size );
               }

               template< typename Input, typename Consumer >
               static void match_array_n( Input& in, Consumer& consumer )
               {
                  in.bump_in_this_line();
                  consumer.begin_array();
                  while( internal::peek_byte_safe( in ) != 0xff ) {
                     match_impl( in, consumer );
                     consumer.element();
                  }
                  in.bump_in_this_line();
                  consumer.end_array();
               }

               template< typename Input, typename Consumer >
               static bool match_array( Input& in, Consumer& consumer )
               {
                  if( internal::peek_minor( in ) != minor_mask ) {
                     match_array_1( in, consumer );
                  }
                  else {
                     match_array_n( in, consumer );
                  }
                  return true;
               }

               template< typename Input, typename Consumer >
               static void match_object_1( Input& in, Consumer& consumer )
               {
                  const auto size = read_size( in );
                  consumer.begin_object( size );
                  for( std::size_t i = 0; i < size; ++i ) {
                     if( internal::peek_major_safe( in ) != major::STRING ) {
                        throw json_pegtl::parse_error( "non-string object key", in );
                     }
                     internal::throw_on_empty( in );
                     if( internal::peek_minor( in ) != minor_mask ) {
                        consumer.key( read_string_1< V, tao::string_view >( in ) );
                     }
                     else {
                        consumer.key( read_string_n< V, std::string >( in, major::STRING ) );
                     }
                     internal::throw_on_empty( in );
                     match_impl( in, consumer );
                     consumer.member();
                  }
                  consumer.end_object( size );
               }

               template< typename Input, typename Consumer >
               static void match_object_n( Input& in, Consumer& consumer )
               {
                  in.bump_in_this_line();
                  consumer.begin_object();
                  while( internal::peek_byte_safe( in ) != 0xff ) {
                     if( internal::peek_major( in ) != major::STRING ) {
                        throw json_pegtl::parse_error( "non-string object key", in );
                     }
                     if( internal::peek_minor( in ) != minor_mask ) {
                        consumer.key( read_string_1< V, tao::string_view >( in ) );
                     }
                     else {
                        consumer.key( read_string_n< V, std::string >( in, major::STRING ) );
                     }
                     internal::throw_on_empty( in );
                     match_impl( in, consumer );
                     consumer.member();
                  }
                  in.bump_in_this_line();
                  consumer.end_object();
               }

               template< typename Input, typename Consumer >
               static bool match_object( Input& in, Consumer& consumer )
               {
                  if( internal::peek_minor( in ) != minor_mask ) {
                     match_object_1( in, consumer );
                  }
                  else {
                     match_object_n( in, consumer );
                  }
                  return true;
               }

               template< typename Input, typename Consumer >
               static bool match_tag( Input& in, Consumer& /*unused*/ )
               {
                  skip_unsigned( in );
                  return true;
               }

               template< typename Floating, typename Input >
               static double read_floating_impl( Input& in )
               {
                  internal::throw_on_empty( in, 1 + sizeof( Floating ) );
                  const Floating result = json::internal::be_to_h< Floating >( in.current() + 1 );
                  in.bump_in_this_line( 1 + sizeof( Floating ) );
                  return result;
               }

               template< typename Input >
               static double read_floating_half_impl( Input& in )
               {
                  internal::throw_on_empty( in, 3 );

                  const int half = ( in.peek_byte( 1 ) << 8 ) + in.peek_byte( 2 );
                  const int exp = ( half >> 10 ) & 0x1f;
                  const int mant = half & 0x3ff;

                  double val;
                  if( exp == 0 ) {
                     val = std::ldexp( mant, -24 );
                  }
                  else if( exp != 31 ) {
                     val = std::ldexp( mant + 1024, exp - 25 );
                  }
                  else {
                     val = ( mant == 0 ) ? INFINITY : NAN;
                  }
                  in.bump_in_this_line( 3 );
                  return half & 0x8000 ? -val : val;
               }

               template< typename Input, typename Consumer >
               static bool match_other( Input& in, Consumer& consumer )
               {
                  switch( internal::peek_minor( in ) ) {
                     case 20:
                        consumer.boolean( false );
                        in.bump_in_this_line();
                        return true;
                     case 21:
                        consumer.boolean( true );
                        in.bump_in_this_line();
                        return true;
                     case 22:
                        consumer.null();
                        in.bump_in_this_line();
                        return true;
                     case 25:
                        consumer.number( read_floating_half_impl( in ) );
                        return true;
                     case 26:
                        consumer.number( read_floating_impl< float >( in ) );
                        return true;
                     case 27:
                        consumer.number( read_floating_impl< double >( in ) );
                        return true;
                     case 24:
                     default:
                        throw json_pegtl::parse_error( "unsupported minor for major 7", in );
                  }
               }
            };

            template< utf8_mode V >
            struct basic_grammar : json_pegtl::must< data< V >, json_pegtl::eof >
            {
            };

            using grammar = basic_grammar< utf8_mode::CHECK >;

         }  // namespace internal

      }  // namespace cbor

   }  // namespace json

}  // namespace tao

#endif