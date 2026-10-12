/* Mathom Notes - shared mathematical symbol catalog.
 * English interface labels, editable French default aliases.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "mathsymbolcatalog.h"
#include <KLocalizedString>

QVector<MathSymbolDefinition> MathSymbolCatalog::entries()
{
    QVector<MathSymbolDefinition> result;
    result.reserve(147);

    {
        const QString category = i18n("Operations");
        result.append({QString::fromUtf8("+"), category, i18n("Plus"), QStringLiteral("plus")});
        result.append({QString::fromUtf8("−"), category, i18n("Minus"), QStringLiteral("moins")});
        result.append({QString::fromUtf8("×"), category, i18n("Multiplication"), QStringLiteral("multiplier")});
        result.append({QString::fromUtf8("÷"), category, i18n("Division"), QStringLiteral("diviser")});
        result.append({QString::fromUtf8("±"), category, i18n("Plus or minus"), QStringLiteral("plusoumoins")});
        result.append({QString::fromUtf8("∓"), category, i18n("Minus or plus"), QStringLiteral("moinsouplus")});
        result.append({QString::fromUtf8("·"), category, i18n("Middle dot"), QStringLiteral("pointmedian")});
        result.append({QString::fromUtf8("⋅"), category, i18n("Dot operator"), QStringLiteral("pointsymbole")});
        result.append({QString::fromUtf8("∗"), category, i18n("Asterisk operator"), QStringLiteral("asterisque")});
        result.append({QString::fromUtf8("∘"), category, i18n("Ring operator"), QStringLiteral("composition")});
        result.append({QString::fromUtf8("%"), category, i18n("Percent"), QStringLiteral("pourcent")});
        result.append({QString::fromUtf8("‰"), category, i18n("Per mille"), QStringLiteral("pourmille")});
    }

    {
        const QString category = i18n("Comparisons and relations");
        result.append({QString::fromUtf8("="), category, i18n("Equal"), QStringLiteral("egal")});
        result.append({QString::fromUtf8("≠"), category, i18n("Not equal"), QStringLiteral("different")});
        result.append({QString::fromUtf8("≈"), category, i18n("Approximately equal"), QStringLiteral("environ")});
        result.append({QString::fromUtf8("≃"), category, i18n("Asymptotically equal"), QStringLiteral("asymptotique")});
        result.append({QString::fromUtf8("≅"), category, i18n("Congruent"), QStringLiteral("congruent")});
        result.append({QString::fromUtf8("≡"), category, i18n("Identical"), QStringLiteral("identique")});
        result.append({QString::fromUtf8("<"), category, i18n("Less than"), QStringLiteral("inferieur")});
        result.append({QString::fromUtf8(">"), category, i18n("Greater than"), QStringLiteral("superieur")});
        result.append({QString::fromUtf8("≤"), category, i18n("Less than or equal"), QStringLiteral("inferieurouegal")});
        result.append({QString::fromUtf8("≥"), category, i18n("Greater than or equal"), QStringLiteral("superieurouegal")});
        result.append({QString::fromUtf8("≪"), category, i18n("Much less than"), QStringLiteral("tresinferieur")});
        result.append({QString::fromUtf8("≫"), category, i18n("Much greater than"), QStringLiteral("tressuperieur")});
        result.append({QString::fromUtf8("∝"), category, i18n("Proportional to"), QStringLiteral("proportionnel")});
    }

    {
        const QString category = i18n("Fractions");
        result.append({QString::fromUtf8("½"), category, i18n("One half"), QStringLiteral("demi")});
        result.append({QString::fromUtf8("⅓"), category, i18n("One third"), QStringLiteral("tiers")});
        result.append({QString::fromUtf8("⅔"), category, i18n("Two thirds"), QStringLiteral("deuxtiers")});
        result.append({QString::fromUtf8("¼"), category, i18n("One quarter"), QStringLiteral("quart")});
        result.append({QString::fromUtf8("¾"), category, i18n("Three quarters"), QStringLiteral("troisquarts")});
        result.append({QString::fromUtf8("⅛"), category, i18n("One eighth"), QStringLiteral("huitieme")});
        result.append({QString::fromUtf8("⅜"), category, i18n("Three eighths"), QStringLiteral("troishuitiemes")});
        result.append({QString::fromUtf8("⅝"), category, i18n("Five eighths"), QStringLiteral("cinqhuitiemes")});
        result.append({QString::fromUtf8("⅞"), category, i18n("Seven eighths"), QStringLiteral("septhuitiemes")});
    }

    {
        const QString category = i18n("Powers and roots");
        result.append({QString::fromUtf8("⁰"), category, i18n("Superscript zero"), QStringLiteral("exposantzero")});
        result.append({QString::fromUtf8("¹"), category, i18n("Superscript one"), QStringLiteral("exposantun")});
        result.append({QString::fromUtf8("²"), category, i18n("Squared"), QStringLiteral("carre")});
        result.append({QString::fromUtf8("³"), category, i18n("Cubed"), QStringLiteral("cube")});
        result.append({QString::fromUtf8("ⁿ"), category, i18n("Superscript n"), QStringLiteral("exposantn")});
        result.append({QString::fromUtf8("⁺"), category, i18n("Superscript plus"), QStringLiteral("exposantplus")});
        result.append({QString::fromUtf8("⁻"), category, i18n("Superscript minus"), QStringLiteral("exposantmoins")});
        result.append({QString::fromUtf8("₀"), category, i18n("Subscript zero"), QStringLiteral("indicezero")});
        result.append({QString::fromUtf8("₁"), category, i18n("Subscript one"), QStringLiteral("indiceun")});
        result.append({QString::fromUtf8("₂"), category, i18n("Subscript two"), QStringLiteral("indicedeux")});
        result.append({QString::fromUtf8("₃"), category, i18n("Subscript three"), QStringLiteral("indicetrois")});
        result.append({QString::fromUtf8("√"), category, i18n("Square root"), QStringLiteral("racine")});
        result.append({QString::fromUtf8("∛"), category, i18n("Cube root"), QStringLiteral("racinecubique")});
        result.append({QString::fromUtf8("∜"), category, i18n("Fourth root"), QStringLiteral("racinequatrieme")});
    }

    {
        const QString category = i18n("Sets");
        result.append({QString::fromUtf8("∈"), category, i18n("Belongs to"), QStringLiteral("appartient")});
        result.append({QString::fromUtf8("∉"), category, i18n("Does not belong to"), QStringLiteral("nappartientpas")});
        result.append({QString::fromUtf8("∋"), category, i18n("Contains as member"), QStringLiteral("contient")});
        result.append({QString::fromUtf8("⊂"), category, i18n("Proper subset"), QStringLiteral("sousensemble")});
        result.append({QString::fromUtf8("⊄"), category, i18n("Not a subset"), QStringLiteral("nonsousensemble")});
        result.append({QString::fromUtf8("⊆"), category, i18n("Subset or equal"), QStringLiteral("sousensembleouegal")});
        result.append({QString::fromUtf8("⊈"), category, i18n("Not a subset or equal"), QStringLiteral("nonsousensembleouegal")});
        result.append({QString::fromUtf8("⊃"), category, i18n("Proper superset"), QStringLiteral("surensemble")});
        result.append({QString::fromUtf8("⊇"), category, i18n("Superset or equal"), QStringLiteral("surensembleouegal")});
        result.append({QString::fromUtf8("∪"), category, i18n("Union"), QStringLiteral("union")});
        result.append({QString::fromUtf8("∩"), category, i18n("Intersection"), QStringLiteral("intersection")});
        result.append({QString::fromUtf8("∅"), category, i18n("Empty set"), QStringLiteral("ensemblevide")});
        result.append({QString::fromUtf8("∖"), category, i18n("Set difference"), QStringLiteral("differenceensembles")});
    }

    {
        const QString category = i18n("Number sets");
        result.append({QString::fromUtf8("ℕ"), category, i18n("Natural numbers"), QStringLiteral("naturels")});
        result.append({QString::fromUtf8("ℤ"), category, i18n("Integers"), QStringLiteral("entiers")});
        result.append({QString::fromUtf8("ℚ"), category, i18n("Rational numbers"), QStringLiteral("rationnels")});
        result.append({QString::fromUtf8("ℝ"), category, i18n("Real numbers"), QStringLiteral("reels")});
        result.append({QString::fromUtf8("ℂ"), category, i18n("Complex numbers"), QStringLiteral("complexes")});
        result.append({QString::fromUtf8("ℍ"), category, i18n("Quaternions"), QStringLiteral("quaternions")});
    }

    {
        const QString category = i18n("Geometry");
        result.append({QString::fromUtf8("∠"), category, i18n("Angle"), QStringLiteral("angle")});
        result.append({QString::fromUtf8("∟"), category, i18n("Right angle"), QStringLiteral("angledroit")});
        result.append({QString::fromUtf8("⊥"), category, i18n("Perpendicular"), QStringLiteral("perpendiculaire")});
        result.append({QString::fromUtf8("∥"), category, i18n("Parallel"), QStringLiteral("parallele")});
        result.append({QString::fromUtf8("∦"), category, i18n("Not parallel"), QStringLiteral("nonparallele")});
        result.append({QString::fromUtf8("△"), category, i18n("Triangle"), QStringLiteral("triangle")});
        result.append({QString::fromUtf8("▽"), category, i18n("Inverted triangle"), QStringLiteral("triangleinverse")});
        result.append({QString::fromUtf8("□"), category, i18n("Square"), QStringLiteral("carregeometrique")});
        result.append({QString::fromUtf8("○"), category, i18n("Circle"), QStringLiteral("cercle")});
        result.append({QString::fromUtf8("°"), category, i18n("Degree"), QStringLiteral("degre")});
        result.append({QString::fromUtf8("′"), category, i18n("Prime"), QStringLiteral("prime")});
        result.append({QString::fromUtf8("″"), category, i18n("Double prime"), QStringLiteral("doubleprime")});
    }

    {
        const QString category = i18n("Analysis");
        result.append({QString::fromUtf8("∞"), category, i18n("Infinity"), QStringLiteral("infini")});
        result.append({QString::fromUtf8("∑"), category, i18n("Summation"), QStringLiteral("somme")});
        result.append({QString::fromUtf8("∏"), category, i18n("Product"), QStringLiteral("produit")});
        result.append({QString::fromUtf8("∫"), category, i18n("Integral"), QStringLiteral("integrale")});
        result.append({QString::fromUtf8("∬"), category, i18n("Double integral"), QStringLiteral("integraledouble")});
        result.append({QString::fromUtf8("∮"), category, i18n("Contour integral"), QStringLiteral("integralecontour")});
        result.append({QString::fromUtf8("∂"), category, i18n("Partial derivative"), QStringLiteral("deriveepartielle")});
        result.append({QString::fromUtf8("∇"), category, i18n("Nabla"), QStringLiteral("nabla")});
        result.append({QString::fromUtf8("∴"), category, i18n("Therefore"), QStringLiteral("donc")});
        result.append({QString::fromUtf8("∵"), category, i18n("Because"), QStringLiteral("parceque")});
    }

    {
        const QString category = i18n("Logic");
        result.append({QString::fromUtf8("∀"), category, i18n("For all"), QStringLiteral("pourtout")});
        result.append({QString::fromUtf8("∃"), category, i18n("There exists"), QStringLiteral("ilexiste")});
        result.append({QString::fromUtf8("∄"), category, i18n("There does not exist"), QStringLiteral("nexistepas")});
        result.append({QString::fromUtf8("∧"), category, i18n("Logical and"), QStringLiteral("etlogique")});
        result.append({QString::fromUtf8("∨"), category, i18n("Logical or"), QStringLiteral("oulogique")});
        result.append({QString::fromUtf8("¬"), category, i18n("Logical not"), QStringLiteral("nonlogique")});
        result.append({QString::fromUtf8("⊕"), category, i18n("Exclusive or"), QStringLiteral("ouexclusif")});
        result.append({QString::fromUtf8("⊢"), category, i18n("Proves"), QStringLiteral("demontre")});
        result.append({QString::fromUtf8("⊨"), category, i18n("Models"), QStringLiteral("satisfait")});
    }

    {
        const QString category = i18n("Arrows and implications");
        result.append({QString::fromUtf8("←"), category, i18n("Left arrow"), QStringLiteral("flechegauche")});
        result.append({QString::fromUtf8("→"), category, i18n("Right arrow"), QStringLiteral("flechedroite")});
        result.append({QString::fromUtf8("↑"), category, i18n("Up arrow"), QStringLiteral("flechehaut")});
        result.append({QString::fromUtf8("↓"), category, i18n("Down arrow"), QStringLiteral("flechebas")});
        result.append({QString::fromUtf8("↔"), category, i18n("Left-right arrow"), QStringLiteral("flechedouble")});
        result.append({QString::fromUtf8("↦"), category, i18n("Maps to"), QStringLiteral("associea")});
        result.append({QString::fromUtf8("⇐"), category, i18n("Implied by"), QStringLiteral("impliquepar")});
        result.append({QString::fromUtf8("⇒"), category, i18n("Implies"), QStringLiteral("implique")});
        result.append({QString::fromUtf8("⇔"), category, i18n("If and only if"), QStringLiteral("equivalent")});
    }

    {
        const QString category = i18n("Statistics and probability");
        result.append({QString::fromUtf8("‱"), category, i18n("Per ten thousand"), QStringLiteral("pourmyriade")});
        result.append({QString::fromUtf8("∼"), category, i18n("Distributed as"), QStringLiteral("distribuecomme")});
        result.append({QString::fromUtf8("𝔼"), category, i18n("Expected value"), QStringLiteral("esperance")});
        result.append({QString::fromUtf8("ℙ"), category, i18n("Probability"), QStringLiteral("probabilite")});
        result.append({QString::fromUtf8("x̄"), category, i18n("Mean of x"), QStringLiteral("moyenne")});
        result.append({QString::fromUtf8("p̂"), category, i18n("Estimated proportion"), QStringLiteral("proportionestimee")});
    }

    {
        const QString category = i18n("Greek letters");
        result.append({QString::fromUtf8("α"), category, i18n("Alpha"), QStringLiteral("alpha")});
        result.append({QString::fromUtf8("β"), category, i18n("Beta"), QStringLiteral("beta")});
        result.append({QString::fromUtf8("γ"), category, i18n("Gamma"), QStringLiteral("gamma")});
        result.append({QString::fromUtf8("δ"), category, i18n("Delta"), QStringLiteral("delta")});
        result.append({QString::fromUtf8("ε"), category, i18n("Epsilon"), QStringLiteral("epsilon")});
        result.append({QString::fromUtf8("ζ"), category, i18n("Zeta"), QStringLiteral("zeta")});
        result.append({QString::fromUtf8("η"), category, i18n("Eta"), QStringLiteral("eta")});
        result.append({QString::fromUtf8("θ"), category, i18n("Theta"), QStringLiteral("theta")});
        result.append({QString::fromUtf8("ι"), category, i18n("Iota"), QStringLiteral("iota")});
        result.append({QString::fromUtf8("κ"), category, i18n("Kappa"), QStringLiteral("kappa")});
        result.append({QString::fromUtf8("λ"), category, i18n("Lambda"), QStringLiteral("lambda")});
        result.append({QString::fromUtf8("μ"), category, i18n("Mu"), QStringLiteral("mu")});
        result.append({QString::fromUtf8("ν"), category, i18n("Nu"), QStringLiteral("nu")});
        result.append({QString::fromUtf8("ξ"), category, i18n("Xi"), QStringLiteral("xi")});
        result.append({QString::fromUtf8("ο"), category, i18n("Omicron"), QStringLiteral("omicron")});
        result.append({QString::fromUtf8("π"), category, i18n("Pi"), QStringLiteral("pi")});
        result.append({QString::fromUtf8("ρ"), category, i18n("Rho"), QStringLiteral("rho")});
        result.append({QString::fromUtf8("σ"), category, i18n("Sigma"), QStringLiteral("sigma")});
        result.append({QString::fromUtf8("τ"), category, i18n("Tau"), QStringLiteral("tau")});
        result.append({QString::fromUtf8("υ"), category, i18n("Upsilon"), QStringLiteral("upsilon")});
        result.append({QString::fromUtf8("φ"), category, i18n("Phi"), QStringLiteral("phi")});
        result.append({QString::fromUtf8("χ"), category, i18n("Chi"), QStringLiteral("chi")});
        result.append({QString::fromUtf8("ψ"), category, i18n("Psi"), QStringLiteral("psi")});
        result.append({QString::fromUtf8("ω"), category, i18n("Omega"), QStringLiteral("omega")});
        result.append({QString::fromUtf8("Γ"), category, i18n("Capital gamma"), QStringLiteral("gammamajuscule")});
        result.append({QString::fromUtf8("Δ"), category, i18n("Capital delta"), QStringLiteral("deltamajuscule")});
        result.append({QString::fromUtf8("Θ"), category, i18n("Capital theta"), QStringLiteral("thetamajuscule")});
        result.append({QString::fromUtf8("Λ"), category, i18n("Capital lambda"), QStringLiteral("lambdamajuscule")});
        result.append({QString::fromUtf8("Ξ"), category, i18n("Capital xi"), QStringLiteral("ximajuscule")});
        result.append({QString::fromUtf8("Π"), category, i18n("Capital pi"), QStringLiteral("pimajuscule")});
        result.append({QString::fromUtf8("Σ"), category, i18n("Capital sigma"), QStringLiteral("sigmamajuscule")});
        result.append({QString::fromUtf8("Φ"), category, i18n("Capital phi"), QStringLiteral("phimajuscule")});
        result.append({QString::fromUtf8("Ψ"), category, i18n("Capital psi"), QStringLiteral("psimajuscule")});
        result.append({QString::fromUtf8("Ω"), category, i18n("Capital omega"), QStringLiteral("omegamajuscule")});
    }

    return result;
}
