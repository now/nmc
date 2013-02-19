<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:string="http://disu.se/software/nml/xsl/include/string"
                exclude-result-prefixes="string">
  <xsl:template mode="move.figures" match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates mode="move.figures" select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template mode="move.figures" match="p|nml/quote|section/quote|item/quote|
                                           definition/quote|table">
    <xsl:for-each select=".//ref[@relation='figure']">
      <xsl:call-template name="create.figure.from.ref"/>
    </xsl:for-each>
    <xsl:copy>
      <xsl:apply-templates mode="remove.figures" select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template name="create.figure.from.ref">
    <figure>
      <xsl:attribute name="class">
        <xsl:variable name="lr">
          <xsl:choose>
            <xsl:when test="position() mod 2 = 0">
              <xsl:text>left</xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <xsl:text>right</xsl:text>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:value-of select="string:push-nmtoken(@class,
                                                  concat('figure-', $lr))"/>
      </xsl:attribute>
      <title><xsl:value-of select="@title"/></title>
      <image uri="{@uri}"><xsl:value-of select="@relation-data"/></image>
    </figure>
  </xsl:template>

  <xsl:template mode="remove.figures" match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates mode="remove.figures" select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template mode="remove.figures" match="ref[@relation='figure']">
    <xsl:apply-templates mode="remove.figures"/>
  </xsl:template>
</xsl:stylesheet>
