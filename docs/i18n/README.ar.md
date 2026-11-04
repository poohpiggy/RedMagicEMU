# RedMagicEMU

RedMagicEMU مشروع مستقل غير رسمي يستند إلى [AEmulator](https://github.com/uxazu/AEmulator)، الذي يستند بدوره إلى [محاكي SENSES الأصلي](https://github.com/Istratii-tech/SENSES). ليس منتجًا رسميًا لـ REDMAGIC ولا يرتبط بها.

يستهدف المشروع برنامج REDMAGIC NX809J الأصلي بنظام Android 16 / API 36 / ARM64 بوصفه النظام الضيف. ينطبق هذا القيد على برنامج الضيف، لا على طراز الجهاز المضيف. لم يُتحقق بعد من الدعم والتشغيل بهذا البرنامج على جهاز؛ راجع [التقدم](../PROGRESS.md) و[خط الأساس](../BASELINE.md).

## البناء

يتطلب مشروع Android الإصدار 36 من Android SDK وNDK r28. استُخدم JDK 21 في بناء خط الأساس:

```shell
./gradlew :app:assembleStandardDebug
```

يستخدم التطوير المحلي معرّف التطبيق `app.redmagicemu`. يُثبّت كتطبيق Android مستقل عن التطبيق الأصلي، ولا تُنقل بياناته الخاصة تلقائيًا. احتفظ بالتطبيق القديم وبياناته إلى أن تُتحقق طريقة للترحيل.

## المساهمون والترخيص

يستند RedMagicEMU إلى AEmulator من تطوير [uxazu](https://github.com/uxazu)، الذي نشأ من محاكيَي HTC Desire HD وHTC One M7 لـ[المؤلف الأصلي](https://t.me/istratii_tech1). نقرّ بعملهم وبإشعارات حقوق النشر والتراخيص القائمة.

يُوزّع المشروع بموجب [GPL-3.0](../../LICENSE). تعود Android وREDMAGIC والعلامات التجارية الأخرى والبرامج الثابتة إلى أصحابها المعنيين.
