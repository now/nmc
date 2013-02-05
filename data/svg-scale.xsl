<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns="http://www.w3.org/2000/svg"
                xmlns:svg="http://www.w3.org/2000/svg">
  <xsl:output method="xml" encoding="utf-8"/>

  <xsl:param name="scale" select="2"/>

  <xsl:template match="/svg:svg">
    <xsl:copy>
      <xsl:copy-of select="@*"/>
      <xsl:attribute name="width">
        <xsl:value-of select="@width*$scale"/>
      </xsl:attribute>
      <xsl:attribute name="height">
        <xsl:value-of select="@height*$scale"/>
      </xsl:attribute>
      <g transform="scale(2)">
        <xsl:copy-of select="node()"/>
      </g>
    </xsl:copy>
  </xsl:template>
</xsl:stylesheet>
