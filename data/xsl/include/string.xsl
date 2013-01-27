<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:func="http://exslt.org/functions"
                xmlns:string="http://disu.se/xsl/string"
                extension-element-prefixes="func">
  <xsl:output method="xml" encoding="utf-8"/>

  <xsl:variable name="string:ascii-uppercase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="string:ascii-lowercase" select="'abcdefghijklmnopqrstuvwxyz'"/>

  <func:function name="string:upcase-ascii">
    <xsl:param name="string"/>

    <func:result select="translate($string,
                                   $string:ascii-lowercase,
                                   $string:ascii-uppercase)"/>
  </func:function>

  <func:function name="string:downcase-ascii">
    <xsl:param name="string"/>

    <func:result select="translate($string,
                                   $string:ascii-uppercase,
                                   $string:ascii-lowercase)"/>
  </func:function>

  <func:function name="string:remove-non-ascii-alpha-numerics">
    <xsl:param name="string"/>

    <func:result select="translate($string,
                                   translate($string,
                                             concat($string:ascii-uppercase,
                                                    $string:ascii-lowercase,
                                                    '1234567890-'),
                                             ''),
                                   '')"/>
  </func:function>
</xsl:stylesheet>
