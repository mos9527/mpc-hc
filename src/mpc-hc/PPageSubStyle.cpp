/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include <algorithm>
#include "mplayerc.h"
#include "MainFrm.h"
#include "PPageSubStyle.h"
#include "../Subtitles/OpenTypeLangTags.h"

// CPPageSubStyle dialog

IMPLEMENT_DYNAMIC(CPPageSubStyle, CMPCThemePPageBase)
CPPageSubStyle::CPPageSubStyle(bool isStyleDialog)
    : CMPCThemePPageBase(CPPageSubStyle::IDD, CPPageSubStyle::IDD)
    , m_stss(AfxGetAppSettings().subtitlesDefStyle)
    , m_bDefaultStyle(true)
    , m_iCharset(0)
    , iOpenTypeLangHint(0)
    , m_angle(0)
    , m_scalex(0)
    , m_scaley(0)
    , m_borderStyle(0)
    , m_bRoundedOpaqueBox(FALSE)
    , m_edgeBlur(0)
    , m_angleX(0)
    , m_angleY(0)
    , m_screenAlignment(0)
    , m_margin(0, 0, 0, 0)
    , m_bLinkAlphaSliders(FALSE)
    , m_iRelativeTo(0)
    , isStyleDialog(isStyleDialog)
#if USE_LIBASS
    , iRenderSSAUsingLibass(false)
#endif
{
}

CPPageSubStyle::~CPPageSubStyle()
{
}

void CPPageSubStyle::InitStyle(const CString& title, const STSStyle& stss)
{
    m_pPSP->pszTitle = m_title = title;
    m_psp.dwFlags |= PSP_USETITLE;

    m_stss = stss;
    m_bDefaultStyle = false;
}

void CPPageSubStyle::AskColor(int i)
{
    CColorDialog dlg(m_stss.colors[i]);
    dlg.m_cc.Flags |= CC_FULLOPEN;
    if (dlg.DoModal() == IDOK) {
        m_stss.colors[i] = dlg.m_cc.rgbResult;
        m_color[i].SetColor(dlg.m_cc.rgbResult);
        SetModified();
    }
}

void CPPageSubStyle::DoDataExchange(CDataExchange* pDX)
{
    CMPCThemePPageBase::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_BUTTON1, m_font);
    DDX_Control(pDX, IDC_BUTTON_TATEGAKIFONT, m_tategakiFont);
    DDX_CBIndex(pDX, IDC_COMBO1, m_iCharset);
    DDX_Control(pDX, IDC_COMBO1, m_cbCharset);
    DDX_CBIndex(pDX, IDC_COMBO2, iOpenTypeLangHint);
    DDX_Control(pDX, IDC_COMBO2, openTypeLangHint);
    DDX_Control(pDX, IDC_EDIT3, m_spacing);
    DDX_Text(pDX, IDC_EDIT4, m_angle);
    DDX_Control(pDX, IDC_SPIN10, m_angleSpin);
    DDX_Text(pDX, IDC_EDIT5, m_scalex);
    DDX_Control(pDX, IDC_SPIN4, m_scalexSpin);
    DDX_Text(pDX, IDC_EDIT6, m_scaley);
    DDX_Control(pDX, IDC_SPIN5, m_scaleySpin);
    DDX_Radio(pDX, IDC_RADIO1, m_borderStyle);
    DDX_Check(pDX, IDC_CHECK_ROUNDEDBOX, m_bRoundedOpaqueBox);
    DDX_Control(pDX, IDC_EDIT1, m_borderWidthX);
    DDX_Control(pDX, IDC_EDIT_OUTLINEY, m_borderWidthY);
    DDX_Control(pDX, IDC_EDIT2, m_shadowDepthX);
    DDX_Control(pDX, IDC_EDIT_SHADOWY, m_shadowDepthY);
    DDX_Control(pDX, IDC_EDIT_BLUR, m_blur);
    DDX_Text(pDX, IDC_EDIT_EDGEBLUR, m_edgeBlur);
    DDX_Control(pDX, IDC_SPIN_EDGEBLUR, m_edgeBlurSpin);
    DDX_Text(pDX, IDC_EDIT_ANGLEX, m_angleX);
    DDX_Control(pDX, IDC_SPIN_ANGLEX, m_angleXSpin);
    DDX_Text(pDX, IDC_EDIT_ANGLEY, m_angleY);
    DDX_Control(pDX, IDC_SPIN_ANGLEY, m_angleYSpin);
    DDX_Control(pDX, IDC_EDIT_SHEARX, m_shearX);
    DDX_Control(pDX, IDC_EDIT_SHEARY, m_shearY);
    DDX_Radio(pDX, IDC_RADIO3, m_screenAlignment);
    DDX_Text(pDX, IDC_EDIT7, m_margin.left);
    DDX_Control(pDX, IDC_SPIN6, m_marginLeftSpin);
    DDX_Text(pDX, IDC_EDIT8, m_margin.right);
    DDX_Control(pDX, IDC_SPIN7, m_marginRightSpin);
    DDX_Text(pDX, IDC_EDIT9, m_margin.top);
    DDX_Control(pDX, IDC_SPIN8, m_marginTopSpin);
    DDX_Text(pDX, IDC_EDIT10, m_margin.bottom);
    DDX_Control(pDX, IDC_SPIN9, m_marginBottomSpin);
    DDX_Control(pDX, IDC_COLORPRI, m_color[0]);
    DDX_Control(pDX, IDC_COLORSEC, m_color[1]);
    DDX_Control(pDX, IDC_COLOROUTL, m_color[2]);
    DDX_Control(pDX, IDC_COLORSHAD, m_color[3]);
    DDX_Slider(pDX, IDC_SLIDER1, m_alpha[0]);
    DDX_Slider(pDX, IDC_SLIDER2, m_alpha[1]);
    DDX_Slider(pDX, IDC_SLIDER3, m_alpha[2]);
    DDX_Slider(pDX, IDC_SLIDER4, m_alpha[3]);
    DDX_Control(pDX, IDC_SLIDER1, m_alphaSliders[0]);
    DDX_Control(pDX, IDC_SLIDER2, m_alphaSliders[1]);
    DDX_Control(pDX, IDC_SLIDER3, m_alphaSliders[2]);
    DDX_Control(pDX, IDC_SLIDER4, m_alphaSliders[3]);
    DDX_Check(pDX, IDC_CHECK1, m_bLinkAlphaSliders);
    DDX_Check(pDX, IDC_CHECK_RELATIVETO, m_iRelativeTo);
#if USE_LIBASS
    DDX_Check(pDX, IDC_CHECK2, iRenderSSAUsingLibass);
#endif
}


BEGIN_MESSAGE_MAP(CPPageSubStyle, CMPCThemePPageBase)
    ON_BN_CLICKED(IDC_BUTTON1, OnChooseFont)
    ON_BN_CLICKED(IDC_BUTTON_TATEGAKIFONT, OnChooseTategakiFont)
    ON_BN_CLICKED(IDC_COLORPRI, OnChoosePrimaryColor)
    ON_BN_CLICKED(IDC_COLORSEC, OnChooseSecondaryColor)
    ON_BN_CLICKED(IDC_COLOROUTL, OnChooseOutlineColor)
    ON_BN_CLICKED(IDC_COLORSHAD, OnChooseShadowColor)
    ON_BN_CLICKED(IDC_CHECK1, OnLinkAlphaSlidersChanged)
    ON_WM_HSCROLL()
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipNotify)
    ON_UPDATE_COMMAND_UI(IDC_CHECK_ROUNDEDBOX, OnUpdateRoundedBox)
END_MESSAGE_MAP()


// CPPageSubStyle message handlers

BOOL CPPageSubStyle::OnInitDialog()
{
    __super::OnInitDialog();

    if (isStyleDialog) {
        GetDlgItem(IDC_STATIC_LIBASS)->ShowWindow(SW_HIDE);
        GetDlgItem(IDC_CHECK2)->ShowWindow(SW_HIDE);
    }

    if (!m_bDefaultStyle) { // the rounded box and the tategaki font are options of the player's default style only
        GetDlgItem(IDC_CHECK_ROUNDEDBOX)->ShowWindow(SW_HIDE);
        m_tategakiFont.ShowWindow(SW_HIDE);
    }

    SetHandCursor(m_hWnd, IDC_COMBO1);

    m_font.SetWindowText(m_stss.fontName);
    m_tategakiFontSpec = AfxGetAppSettings().lyricsTategakiFont;
    UpdateTategakiFontButton();
    m_iCharset = -1;
    for (int i = 0; i < CharSetLen; i++) {
        CString str;
        str.Format(_T("%s (%u)"), CharSetNames[i], CharSetList[i]);
        m_cbCharset.AddString(str);
        m_cbCharset.SetItemData(i, CharSetList[i]);
        if (m_stss.charSet == CharSetList[i]) {
            m_iCharset = i;
        }
    }

    iOpenTypeLangHint = -1;
    auto& s = AfxGetAppSettings();
    for (int i = 0, t = 0; i < _countof(OpenTypeLang::OpenTypeLangTags); i++) {
        CString str;
        CStringA lang(OpenTypeLang::OpenTypeLangTags[i].lang);
        if (lang.GetLength() == 2) {
            str.Format(_T("%ls (%hs)"), OpenTypeLang::OpenTypeLangTags[i].langDescription, lang);
            openTypeLangHint.AddString(str);
            openTypeLangHint.SetItemData(t, i);
            if (strncmp(s.strOpenTypeLangHint, OpenTypeLang::OpenTypeLangTags[i].lang, OpenTypeLang::OTLangHintLen) == 0) {
                iOpenTypeLangHint = t;
            }
            t++;
        }
    }

#if USE_LIBASS
    iRenderSSAUsingLibass = s.bRenderSSAUsingLibass;
#else
    GetDlgItem(IDC_STATIC_LIBASS)->ShowWindow(false);
    GetDlgItem(IDC_CHECK2)->ShowWindow(false);
#endif

    // TODO: allow floats in these edit boxes
    m_spacing.SetRange(-100.0f, 100.0f);
    m_spacing = m_stss.fontSpacing;
    // the edits take whole degrees in [0, 360)
    auto angle = [](double a) {
        a = std::fmod(a, 360.0);
        return (int)(a < 0 ? a + 360.0 : a) % 360;
    };
    m_angle = angle(m_stss.fontAngleZ);
    m_angleSpin.SetRange32(0, 359);
    m_angleX = angle(m_stss.fontAngleX);
    m_angleXSpin.SetRange32(0, 359);
    m_angleY = angle(m_stss.fontAngleY);
    m_angleYSpin.SetRange32(0, 359);
    m_scalex = (int)m_stss.fontScaleX;
    m_scalexSpin.SetRange32(-10000, 10000);
    m_scaley = (int)m_stss.fontScaleY;
    m_scaleySpin.SetRange32(-10000, 10000);

    m_borderStyle = m_stss.borderStyle;
    m_bRoundedOpaqueBox = s.bSubtitleRoundedOpaqueBox;
    m_borderWidthX.SetRange(0.0f, 100.0f);
    m_borderWidthX = m_stss.outlineWidthX;
    m_borderWidthY.SetRange(0.0f, 100.0f);
    m_borderWidthY = m_stss.outlineWidthY;
    // the shadow is offset by these, negative ones place it up or left
    m_shadowDepthX.SetRange(-100.0f, 100.0f);
    m_shadowDepthX = m_stss.shadowDepthX;
    m_shadowDepthY.SetRange(-100.0f, 100.0f);
    m_shadowDepthY = m_stss.shadowDepthY;

    m_blur.SetRange(0.0f, 100.0f);
    m_blur = m_stss.fGaussianBlur;
    m_edgeBlur = std::clamp(m_stss.fBlur, 0, 100);
    m_edgeBlurSpin.SetRange32(0, 100);
    m_shearX.SetRange(-10.0f, 10.0f);
    m_shearX = m_stss.fontShiftX;
    m_shearY.SetRange(-10.0f, 10.0f);
    m_shearY = m_stss.fontShiftY;

    m_screenAlignment = m_stss.scrAlignment - 1;
    m_margin = m_stss.marginRect;
    m_marginLeftSpin.SetRange32(-10000, 10000);
    m_marginRightSpin.SetRange32(-10000, 10000);
    m_marginTopSpin.SetRange32(-10000, 10000);
    m_marginBottomSpin.SetRange32(-10000, 10000);
    m_iRelativeTo = m_stss.relativeTo;

    for (size_t i = 0; i < m_color.size(); i++) {
        m_color[i].SetColor(m_stss.colors[i]);
        m_alpha[i] = BYTE_MAX - m_stss.alpha[i];
        m_alphaSliders[i].SetRange(0, BYTE_MAX);
    }

    m_bLinkAlphaSliders = FALSE;

    UpdateData(FALSE);

    AdjustDynamicWidgets();
    EnableThemedDialogTooltips(this);
    CreateToolTip();
    if (m_bDefaultStyle) {
        m_wndToolTip.AddTool(GetDlgItem(IDC_CHECK_RELATIVETO), ResStr(IDS_TEXT_SUB_RENDERING_TARGET));
        m_wndToolTip.AddTool(&m_tategakiFont, ResStr(IDS_SUBSTYLE_TATEGAKI_FONT_TIP));
    }

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPageSubStyle::OnApply()
{
    UpdateData();

    if (m_iCharset >= 0) {
        m_stss.charSet = (int)m_cbCharset.GetItemData(m_iCharset);
    }
    auto& s = AfxGetAppSettings(); 
    if (iOpenTypeLangHint >= 0) {
        int i = (int) openTypeLangHint.GetItemData(iOpenTypeLangHint);
        s.strOpenTypeLangHint = OpenTypeLang::OpenTypeLangTags[i].lang;
    }

#if USE_LIBASS
    s.bRenderSSAUsingLibass = iRenderSSAUsingLibass;
#endif
    m_stss.fontSpacing = m_spacing;
    m_stss.fontAngleZ = m_angle;
    m_stss.fontAngleX = m_angleX;
    m_stss.fontAngleY = m_angleY;
    m_stss.fontScaleX = m_scalex;
    m_stss.fontScaleY = m_scaley;
    m_stss.fontShiftX = m_shearX;
    m_stss.fontShiftY = m_shearY;

    m_stss.borderStyle = m_borderStyle;
    m_stss.outlineWidthX = m_borderWidthX;
    m_stss.outlineWidthY = m_borderWidthY;
    m_stss.shadowDepthX = m_shadowDepthX;
    m_stss.shadowDepthY = m_shadowDepthY;
    m_stss.fGaussianBlur = m_blur;
    m_stss.fBlur = std::clamp(m_edgeBlur, 0, 100);

    m_stss.scrAlignment = m_screenAlignment + 1;
    m_stss.marginRect = m_margin;
    m_stss.relativeTo = (STSStyle::RelativeTo)m_iRelativeTo;

    for (size_t i = 0; i < m_alpha.size(); i++) {
        ASSERT(m_alpha[i] <= BYTE_MAX);
        m_stss.alpha[i] = BYTE_MAX - BYTE(m_alpha[i]);
    }

    if (m_bDefaultStyle) {
        STSStyle& stss = AfxGetAppSettings().subtitlesDefStyle;
        if (stss != m_stss || s.bSubtitleRoundedOpaqueBox != !!m_bRoundedOpaqueBox || s.lyricsTategakiFont != m_tategakiFontSpec) {
            AfxGetAppSettings().subtitlesDefStyle = m_stss;
            s.bSubtitleRoundedOpaqueBox = !!m_bRoundedOpaqueBox;
            s.lyricsTategakiFont = m_tategakiFontSpec;
            if (CMainFrame* pMainFrame = AfxGetMainFrame()) {
                pMainFrame->UpdateSubtitleRenderingParameters();
                pMainFrame->RepaintVideo();
            }
        }
    }

    return __super::OnApply();
}

void CPPageSubStyle::OnChooseFont()
{
    UpdateData();

    LOGFONT lf;
    lf <<= m_stss;
    if (m_iCharset >= 0) {
        lf.lfCharSet = (BYTE)m_cbCharset.GetItemData(m_iCharset);
    }
    BYTE prev_charset = lf.lfCharSet;

    // the effects add underline and strikeout, and a text color: the primary color
    const COLORREF primary = m_stss.colors[0];
    CFontDialog dlg(&lf, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_SCALABLEONLY | CF_EFFECTS);
    dlg.m_cf.rgbColors = primary;
    if (dlg.DoModal() == IDOK) {
        // its list has 16 colors; one not listed is kept unless another is picked
        if (dlg.GetColor() != primary) {
            m_stss.colors[0] = dlg.GetColor();
            m_color[0].SetColor(m_stss.colors[0]);
        }

        CString str(lf.lfFaceName);
        if (str.GetLength() > 16) {
            str = str.Left(14) + _T("...");
        }
        m_font.SetWindowText(str);

        if (lf.lfCharSet == ANSI_CHARSET && prev_charset != ANSI_CHARSET) {
            lf.lfCharSet = prev_charset;
        }

        for (int i = 0, count = m_cbCharset.GetCount(); i < count; i++) {
            if (m_cbCharset.GetItemData(i) == lf.lfCharSet) {
                m_cbCharset.SetCurSel(i);
                break;
            }
        }

        m_stss = lf;
        // it may follow this one
        UpdateTategakiFontButton();

        SetModified();
    }
}

void CPPageSubStyle::UpdateTategakiFontButton()
{
    STSStyle style = m_stss;
    m_tategakiFontSpec.ApplyTo(style);
    CString face = style.fontName;
    face.TrimLeft(_T('@'));
    if (face.GetLength() > 12) {
        face = face.Left(10) + _T("...");
    }
    CString text;
    text.Format(ResStr(IDS_SUBSTYLE_TATEGAKI_FONT), face.GetString(), style.fontSize);
    m_tategakiFont.SetWindowText(text);
}

void CPPageSubStyle::OnChooseTategakiFont()
{
    CString mainFace = m_stss.fontName;
    mainFace.TrimLeft(_T('@'));

    // the dialog starts from the font the lyrics use: the tategaki one, or Font's
    STSStyle current = m_stss;
    current.fontName = mainFace;
    m_tategakiFontSpec.ApplyTo(current);
    LOGFONT lf;
    lf <<= current;
    // the vertical "@" faces are not listed, the lyrics use the vertical form of the face picked
    CFontDialog dlg(&lf, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_SCALABLEONLY | CF_NOVERTFONTS);
    if (dlg.DoModal() == IDOK) {
        // as the main font takes it from the dialog
        STSStyle picked;
        picked = lf;
        CString face = picked.fontName;
        face.TrimLeft(_T('@'));

        CAppSettings::LyricsTategakiFont spec;
        // the same as Font: follow it, also when it changes
        const bool bSame = face.CompareNoCase(mainFace) == 0
                           && std::abs(picked.fontSize - m_stss.fontSize) < 0.01
                           && picked.fontWeight == m_stss.fontWeight
                           && !!picked.fItalic == !!m_stss.fItalic;
        if (!bSame) {
            spec.face = face;
            spec.size = picked.fontSize;
            spec.weight = picked.fontWeight;
            spec.italic = picked.fItalic ? 1 : 0;
        }
        m_tategakiFontSpec = spec;
        UpdateTategakiFontButton();
        SetModified();
    }
}

void CPPageSubStyle::OnChoosePrimaryColor()
{
    AskColor(0);
}

void CPPageSubStyle::OnChooseSecondaryColor()
{
    AskColor(1);
}

void CPPageSubStyle::OnChooseOutlineColor()
{
    AskColor(2);
}

void CPPageSubStyle::OnChooseShadowColor()
{
    AskColor(3);
}

void CPPageSubStyle::OnLinkAlphaSlidersChanged()
{
    UpdateData();

    if (m_bLinkAlphaSliders) {
        int avgAlpha = 0;
        for (const auto& alphaSlider : m_alphaSliders) {
            avgAlpha += alphaSlider.GetPos();
        }
        avgAlpha /= 4;
        for (auto& alphaSlider : m_alphaSliders) {
            alphaSlider.SetPos(avgAlpha);
        }
    }

    SetModified();
}

void CPPageSubStyle::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    //note, this code assumes that the only sliders are the 4 alphas
    if (m_bLinkAlphaSliders && pScrollBar) {
        int pos = ((CSliderCtrl*)pScrollBar)->GetPos();
        for (auto& alphaSlider : m_alphaSliders) {
            alphaSlider.SetPos(pos);
        }
    }
    UpdateData(); //if we don't do this, m_alpha will not be updated for tooltips
    RedrawDialogTooltipIfVisible(); //if the scroll is caused by a wheel or arrows, the default tooltip may be active due to hover, in which case, we want to update
    SetModified();

    __super::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CPPageSubStyle::OnUpdateRoundedBox(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(IsDlgButtonChecked(IDC_RADIO2));
}

void CPPageSubStyle::AdjustDynamicWidgets() {
    AdjustDynamicWidgetPair(this, IDC_STATIC1, IDC_EDIT3);
    AdjustDynamicWidgetPair(this, IDC_STATIC2, IDC_EDIT4);
    AdjustDynamicWidgetPair(this, IDC_STATIC3, IDC_EDIT5);
    AdjustDynamicWidgetPair(this, IDC_STATIC4, IDC_EDIT6);
    AdjustDynamicWidgetPair(this, IDC_STATIC5, IDC_EDIT1);
    AdjustDynamicWidgetPair(this, IDC_STATIC6, IDC_EDIT2);
    AdjustDynamicWidgetPair(this, IDC_STATIC7, IDC_EDIT7);
    AdjustDynamicWidgetPair(this, IDC_STATIC8, IDC_EDIT8);
    AdjustDynamicWidgetPair(this, IDC_STATIC9, IDC_EDIT9);
    AdjustDynamicWidgetPair(this, IDC_STATIC10, IDC_EDIT10);
    AdjustDynamicWidgetGroup(this, { { IDC_STATIC_BLUR, IDC_EDIT_BLUR }, { IDC_STATIC_ANGLEX, IDC_EDIT_ANGLEX },
                                     { IDC_STATIC_SHEARX, IDC_EDIT_SHEARX } });
    AdjustDynamicWidgetGroup(this, { { IDC_STATIC_EDGEBLUR, IDC_EDIT_EDGEBLUR }, { IDC_STATIC_ANGLEY, IDC_EDIT_ANGLEY },
                                     { IDC_STATIC_SHEARY, IDC_EDIT_SHEARY } });
}


BOOL CPPageSubStyle::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult) {
    LPTOOLTIPTEXT pTTT = reinterpret_cast<LPTOOLTIPTEXT>(pNMHDR);

    UINT_PTR nID = pNMHDR->idFrom;
    if (pTTT->uFlags & TTF_IDISHWND) {
        nID = ::GetDlgCtrlID((HWND)nID);
    }

    BOOL bRet = FALSE;

    if (nID == IDC_SLIDER1 || nID == IDC_SLIDER2 || nID == IDC_SLIDER3 || nID == IDC_SLIDER4) {
        bRet = TRUE;

        CString strTipText;
        int alpha = 0;
        if (nID == IDC_SLIDER1) {
            alpha = m_alpha[0];
        } else if (nID == IDC_SLIDER2) {
            alpha = m_alpha[1];
        } else if (nID == IDC_SLIDER3) {
            alpha = m_alpha[2];
        } else if (nID == IDC_SLIDER4) {
            alpha = m_alpha[3];
        }
        strTipText.Format(IDS_VOLUME, int(float(alpha) * 100 / 255));
        _tcscpy_s(pTTT->szText, strTipText.Left(_countof(pTTT->szText) - 1));
    }

    if (bRet) {
        PlaceThemedDialogTooltip(nID);
    }

    return bRet;
}
