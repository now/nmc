<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:date="http://exslt.org/dates-and-times"
                xmlns:exsl="http://exslt.org/common"
                xmlns:func="http://exslt.org/functions"
                xmlns:man="http://disu.se/software/nml/xsl/man"
                xmlns:str="http://exslt.org/strings"
                xmlns:string="http://disu.se/software/nml/xsl/include/string"
                extension-element-prefixes="date exsl func str">
  <xsl:import href="include/figures.xsl"/>
  <xsl:include href="include/string.xsl"/>

  <xsl:output method="text" encoding="utf-8"/>

  <xsl:strip-space elements="nml section itemization enumeration definitions
                             item term definition table head body row"/>

  <xsl:param name="section" select="1"/>
  <xsl:param name="date">
    <xsl:variable name="d" select="date:date()"/>
    <xsl:value-of select="concat(date:day-in-month($d), ' ',
                                 date:month-name($d), ', ', date:year($d))"/>
  </xsl:param>
  <xsl:param name="source"/>
  <xsl:param name="manual" select="'User Commands'"/>

  <xsl:param name="man:indent" select="4"/>
  <xsl:param name="man:indent.list" select="$man:indent"/>
  <xsl:param name="man:indent.code" select="$man:indent"/>
  <xsl:param name="man:indent.table" select="$man:indent"/>
  <xsl:param name="man:indent.figure" select="$man:indent"/>

  <xsl:variable name="man:param-escapes-rfc">
    <escape what="\" with="\e"/>
    <escape what='"' with='\(dq'/>
  </xsl:variable>

  <xsl:variable name="man:param-escapes"
                select="exsl:node-set($man:param-escapes-rfc)"/>

  <func:function name="man:param">
    <xsl:param name="string"/>
    <xsl:param name="escape" select="true()"/>

    <xsl:variable name="escaped">
      <xsl:choose>
        <xsl:when test="$escape">
          <xsl:value-of select="str:replace(.,
                                            $man:param-escapes/@what,
                                            $man:param-escapes/@with)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$string"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>

    <xsl:choose>
      <xsl:when test="not($string) or contains($string, ' ')">
        <func:result select="concat('&quot;', $string, '&quot;')"/>
      </xsl:when>
      <xsl:otherwise>
        <func:result select="$string"/>
      </xsl:otherwise>
    </xsl:choose>
  </func:function>

  <xsl:template match="nml">
    <xsl:variable name="moved">
      <xsl:apply-templates mode="move.figures"/>
    </xsl:variable>
    <xsl:apply-templates select="exsl:node-set($moved)"/>
  </xsl:template>

  <xsl:template match="nml/title">
    <xsl:variable name="title">
      <xsl:choose>
        <xsl:when test="substring-before(., '(') != ''">
          <xsl:value-of select="substring-before(., '(')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="."/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:text>.TH </xsl:text>
    <xsl:value-of select="man:param(string:upcase-ascii($title))"/>
    <xsl:text> </xsl:text>
    <xsl:choose>
      <xsl:when test="substring-before(substring-after(., '('), ')') != ''">
        <xsl:value-of select="substring-before(substring-after(., '('), ')')"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="$section"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:text> </xsl:text>
    <xsl:value-of select="man:param($date)"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="man:param($source)"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="man:param($manual)"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="title"/>

  <xsl:template match="nml/section">
    <xsl:text>.SH </xsl:text>
    <xsl:value-of select="man:param(string:upcase-ascii(title))"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="nml/section/section">
    <xsl:text>.SS </xsl:text>
    <xsl:value-of select="man:param(title)"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="section">
    <xml:message terminate="yes">
      <xsl:text>man.xsl: can’t nest sections more than two levels </xsl:text>
      <xsl:text>deep in man pages</xsl:text>
    </xml:message>
  </xsl:template>

  <xsl:template match="p">
    <xsl:text>.sp&#10;</xsl:text>
    <xsl:call-template name="normalize-content"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template name="normalize-content">
    <xsl:variable name="content">
      <xsl:apply-templates/>
    </xsl:variable>
    <xsl:value-of select="normalize-space($content)"/>
  </xsl:template>

  <xsl:variable name="man:text-escapes-rfc">
    <escape what="\" with="\e"/>
    <escape what="-" with="\-"/>
  </xsl:variable>

  <xsl:variable name="man:text-escapes"
                select="exsl:node-set($man:text-escapes-rfc)"/>

  <xsl:template match="text()">
    <xsl:value-of select="str:replace(.,
                                      $man:text-escapes/@what,
                                      $man:text-escapes/@with)"/>
  </xsl:template>

  <xsl:template match="itemization/item">
    <xsl:call-template name="item">
      <xsl:with-param name="designator" select="'\(bu'"/>
      <xsl:with-param name="length" select="1"/>
      <xsl:with-param name="indent" select="2.3"/>
    </xsl:call-template>
  </xsl:template>

  <xsl:template name="item">
    <xsl:param name="designator"/>
    <xsl:param name="length" select="string-length($designator)"/>
    <xsl:param name="indent"/>

    <xsl:text>.sp&#10;.RS </xsl:text>
    <xsl:value-of select="$man:indent.list"/>
    <xsl:text>&#10;.ie n \{\&#10;\h'-0</xsl:text>
    <xsl:value-of select="$man:indent.list"/>
    <xsl:text>'</xsl:text>
    <xsl:value-of select="$designator"/>
    <xsl:text>\h'+0</xsl:text>
    <xsl:value-of select="$man:indent.list - $length"/>
    <xsl:text>'\c&#10;.\}&#10;.el \{\&#10;</xsl:text>
    <xsl:text>.sp -1&#10;.IP </xsl:text>
    <xsl:value-of select="man:param($designator, false)"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="$indent"/>
    <xsl:text>&#10;.\}&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="enumeration/item">
    <xsl:call-template name="item">
      <xsl:with-param name="designator">
        <xsl:if test="count(preceding-sibling::item) &lt; 9">
          <xsl:text> </xsl:text>
        </xsl:if>
        <xsl:number format="1."/>
      </xsl:with-param>
      <xsl:with-param name="indent" select="4.2"/>
    </xsl:call-template>
  </xsl:template>

  <xsl:template match="itemization[ancestor::item]|enumeration[ancestor::item]">
    <xsl:apply-templates/>
    <xsl:if test="following-sibling::node() or
      parent::p[following-sibling::node()]">
      <xsl:text>.sp</xsl:text>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="definitions[item/definition//item]">
    <xsl:apply-templates mode="definitions-with-lists" select="."/>
  </xsl:template>

  <xsl:template mode="definitions-with-lists" match="term">
    <xsl:text>.PP&#10;\fB</xsl:text>
    <xsl:call-template name="normalize-content"/>
    <xsl:text>\fP&#10;</xsl:text>
  </xsl:template>

  <xsl:template mode="definitions-with-lists" match="definition">
    <xsl:text>.RS </xsl:text>
    <xsl:value-of select="$man:indent.list"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="definitions">
    <xsl:apply-templates/>
    <xsl:text>.PP&#10;.sp -1&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="term">
    <xsl:text>.TP&#10;\fB</xsl:text>
    <xsl:call-template name="normalize-content"/>
    <xsl:text>\fP&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="definition">
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="p[ancestor::item]">
    <xsl:call-template name="normalize-content"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:if test="following-sibling::*[1][self::p]">
      <xsl:text>.sp&#10;</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:variable name="man:indent.quote" select="4"/>

  <xsl:template match="nml/quote|section/quote|item/quote|definition/quote">
    <xsl:text>.sp&#10;.RS </xsl:text>
    <xsl:value-of select="$man:indent.quote"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="*"/>
    <xsl:text>.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="line">
    <xsl:call-template name="normalize-content"/>
    <xsl:text>&#10;.br&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="attribution">
    <xsl:text>\(em\ </xsl:text>
    <xsl:call-template name="normalize-content"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:variable name="man:additional-code-escapes-rfc">
    <escape what="&#10;." with="&#10;\&amp;."/>
    <escape what="&#10;'" with="&#10;\&amp;'"/>
  </xsl:variable>

  <xsl:variable name="man:code-escapes"
                select="$man:text-escapes|
                        exsl:node-set($man:additional-code-escapes-rfc)"/>

  <xsl:template match="section[title='Synopsis']/code" name="code" priority="1">
    <xsl:text>.nf&#10;</xsl:text>
    <xsl:value-of select="str:replace(., $man:code-escapes/@what,
                                      $man:code-escapes/@with)"/>
    <xsl:text>&#10;.fi&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="nml/code|section/code|item/code|definition/code">
    <xsl:text>.sp&#10;.RS </xsl:text>
    <xsl:value-of select="$man:indent.code"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:call-template name="code"/>
    <xsl:text>.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="table">
    <xsl:text>.RS </xsl:text>
    <xsl:value-of select="$man:indent.table"/>
    <xsl:text>&#10;.TS&#10;</xsl:text>
    <xsl:if test="head">
      <xsl:apply-templates mode="table.format" select="head/row"/>
      <xsl:apply-templates select="head/row"/>
      <xsl:text>.T&amp;&#10;</xsl:text>
    </xsl:if>
    <xsl:apply-templates mode="table.format" select="body/row"/>
    <xsl:apply-templates select="body/row"/>
    <xsl:text>.TE&#10;.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template mode="table.format" match="row">
    <xsl:apply-templates mode="table.format"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template mode="table.format" match="row[position()=last()]">
    <xsl:apply-templates mode="table.format"/>
    <xsl:text>.&#10;</xsl:text>
  </xsl:template>

  <xsl:template mode="table.format" match="head/row/cell[1]"
                name="table.format.head.cell" priority="1">
    <xsl:text>cB</xsl:text>
  </xsl:template>

  <xsl:template mode="table.format" match="head/row/cell">
    <xsl:text> </xsl:text>
    <xsl:call-template name="table.format.head.cell"/>
  </xsl:template>

  <xsl:template mode="table.format" match="cell[1]" name="table.format.cell">
    <xsl:text>l</xsl:text>
  </xsl:template>

  <xsl:template mode="table.format" match="cell">
    <xsl:text> </xsl:text>
    <xsl:call-template name="table.format.cell"/>
  </xsl:template>

  <xsl:template match="row">
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="cell[1]" name="cell">
    <xsl:text>T{&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;T}</xsl:text>
  </xsl:template>

  <xsl:template match="cell">
    <xsl:text>&#9;</xsl:text>
    <xsl:call-template name="cell"/>
  </xsl:template>

  <xsl:template match="figure">
    <xsl:text>.PP&#10;.RS </xsl:text>
    <xsl:value-of select="$man:indent.list"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>.RE&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="figure/title">
    <xsl:text>.sp&#10;\fB</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>\fR&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="image">
    <xsl:text>[IMAGE </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>]&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="emphasis">
    <xsl:text>\fI</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="code">
    <xsl:text>\FC</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>\F[]</xsl:text>
  </xsl:template>

  <xsl:template match="ref">
    <xsl:apply-templates/>
    <xsl:text> (</xsl:text>
    <xsl:if test="@title">
      <xsl:value-of select="@title"/>
      <xsl:text> at </xsl:text>
    </xsl:if>
    <xsl:text>\fB</xsl:text>
    <xsl:value-of select="@uri"/>
    <xsl:text>\fR)</xsl:text>
  </xsl:template>
</xsl:stylesheet>
