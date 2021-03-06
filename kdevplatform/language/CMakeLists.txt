
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9505 )

set(kdevplatformlanguage_LIB_SRCS
    editor/modificationrevisionset.cpp
    editor/documentrange.cpp
    editor/documentrangeobject.cpp
    editor/documentcursor.cpp
    editor/documentcursorobject.cpp
    editor/editorintegrator.cpp
    editor/editorintegratorstatic.cpp
    editor/hashedstring.cpp
    editor/modificationrevision.cpp
    editor/editorrevisiontracker.cpp
    backgroundparser/backgroundparser.cpp
    backgroundparser/parsejob.cpp
    backgroundparser/parserdependencypolicy.cpp
    backgroundparser/documentchangetracker.cpp
    backgroundparser/parseprojectjob.cpp
    backgroundparser/urlparselock.cpp

    duchain/referencecounting.cpp
    duchain/specializationstore.cpp
    duchain/codemodel.cpp
    duchain/duchain.cpp
    duchain/waitforupdate.cpp
    duchain/duchainpointer.cpp
    duchain/ducontext.cpp
    duchain/topducontext.cpp
    duchain/topducontextdynamicdata.cpp
    duchain/functiondefinition.cpp
    duchain/declaration.cpp
    duchain/classmemberdeclaration.cpp
    duchain/classfunctiondeclaration.cpp
    duchain/classdeclaration.cpp
    duchain/use.cpp
    duchain/smartconverter.cpp
    duchain/forwarddeclaration.cpp
    duchain/duchainbase.cpp
    duchain/duchainlock.cpp
    duchain/identifier.cpp
    duchain/parsingenvironment.cpp
    duchain/abstractfunctiondeclaration.cpp
    duchain/functiondeclaration.cpp
    duchain/stringhelpers.cpp
    duchain/namespacealiasdeclaration.cpp
    duchain/aliasdeclaration.cpp
    duchain/dumpdotgraph.cpp
    duchain/duchainobserver.cpp
    duchain/duchainutils.cpp
    duchain/declarationid.cpp
    duchain/definitions.cpp
    duchain/uses.cpp
    duchain/importers.cpp
    duchain/dumpchain.cpp
    duchain/indexedstring.cpp
    duchain/duchainregister.cpp
    duchain/persistentsymboltable.cpp
    duchain/instantiationinformation.cpp

    duchain/types/typesystem.cpp
    duchain/types/typeregister.cpp
    duchain/types/identifiedtype.cpp
    duchain/types/abstracttype.cpp
    duchain/types/integraltype.cpp
    duchain/types/functiontype.cpp
    duchain/types/structuretype.cpp
    duchain/types/pointertype.cpp
    duchain/types/referencetype.cpp
    duchain/types/delayedtype.cpp
    duchain/types/arraytype.cpp
    duchain/types/indexedtype.cpp
    duchain/types/enumerationtype.cpp
    duchain/types/constantintegraltype.cpp
    duchain/types/enumeratortype.cpp
    duchain/types/typeutils.cpp
    duchain/types/typealiastype.cpp
    duchain/types/unsuretype.cpp

    duchain/repositories/itemrepository.cpp
    duchain/repositories/typerepository.cpp

    duchain/navigation/problemnavigationcontext.cpp
    duchain/navigation/abstractnavigationwidget.cpp
    duchain/navigation/abstractnavigationcontext.cpp
    duchain/navigation/usesnavigationcontext.cpp
    duchain/navigation/abstractdeclarationnavigationcontext.cpp
    duchain/navigation/abstractincludenavigationcontext.cpp
    duchain/navigation/useswidget.cpp
    duchain/navigation/usescollector.cpp

    interfaces/iastcontainer.cpp
    interfaces/ilanguagesupport.cpp
    interfaces/quickopendataprovider.cpp
    interfaces/iproblem.cpp
    interfaces/iquickopen.cpp
    interfaces/editorcontext.cpp
    interfaces/codecontext.cpp

    codecompletion/codecompletion.cpp
    codecompletion/codecompletionworker.cpp
    codecompletion/codecompletionmodel.cpp
    codecompletion/codecompletionitem.cpp
    codecompletion/codecompletioncontext.cpp
    codecompletion/codecompletionitemgrouper.cpp
    codecompletion/codecompletionhelper.cpp
    codecompletion/normaldeclarationcompletionitem.cpp

    codegen/applychangeswidget.cpp
    codegen/createclass.cpp
    #codegen/codegenerator.cpp
    codegen/coderepresentation.cpp
    codegen/documentchangeset.cpp
    codegen/duchainchangeset.cpp
    codegen/komparesupport.cpp
    codegen/overridespage.cpp
    codegen/utilities.cpp

    util/setrepository.cpp
	util/includeitem.cpp
    util/navigationtooltip.cpp

    highlighting/colorcache.cpp
    highlighting/configurablecolors.cpp
    highlighting/codehighlighting.cpp
)

set( kdevplatformlanguage_LIB_UI
    codegen/ui/newclass.ui
    codegen/ui/overridevirtuals.ui
    codegen/ui/licensechooser.ui
    codegen/ui/outputlocation.ui
)

kde4_add_ui_files( kdevplatformlanguage_LIB_SRCS ${kdevplatformlanguage_LIB_UI} )


kde4_add_library(kdevplatformlanguage SHARED ${kdevplatformlanguage_LIB_SRCS})
target_link_libraries(kdevplatformlanguage
        ${KDE4_KPARTS_LIBS}
        ${KDE4_KTEXTEDITOR_LIBS}
        ${KDE4_THREADWEAVER_LIBRARIES}
        kdevplatforminterfaces
        kdevplatformutil)
# Need to add kdevplatforminterfaces and kdevplatformutil when those are exported targets
target_link_libraries(kdevplatformlanguage LINK_INTERFACE_LIBRARIES
        ${KDE4_KTEXTEDITOR_LIBS}
        ${KDE4_THREADWEAVER_LIBRARIES})
set_target_properties(kdevplatformlanguage PROPERTIES VERSION ${KDEVPLATFORM_LIB_VERSION} SOVERSION ${KDEVPLATFORM_LIB_SOVERSION})
install(TARGETS kdevplatformlanguage EXPORT KDevPlatformTargets ${INSTALL_TARGETS_DEFAULT_ARGS} )

########### install files ###############

install(FILES
    languageexport.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language COMPONENT Devel
)

install(FILES
    interfaces/ilanguagesupport.h
    interfaces/icodehighlighting.h
    interfaces/quickopendataprovider.h
    interfaces/quickopenfilter.h
    interfaces/iquickopen.h
    interfaces/iproblem.h
    interfaces/codecontext.h
    interfaces/editorcontext.h
    interfaces/iastcontainer.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/interfaces COMPONENT Devel
)

install(FILES
    editor/documentrange.h
    editor/documentrangeobject.h
    editor/documentcursor.h
    editor/documentcursorobject.h
    editor/editorintegrator.h
    editor/hashedstring.h
    editor/simplecursor.h
    editor/simplerange.h
    editor/modificationrevision.h
    editor/modificationrevisionset.h
    editor/editorrevisiontracker.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/editor COMPONENT Devel
)

install(FILES
    backgroundparser/backgroundparser.h
    backgroundparser/parsejob.h
    backgroundparser/urlparselock.h
    backgroundparser/documentchangetracker.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/backgroundparser COMPONENT Devel
)

install(FILES
    util/navigationtooltip.h
    util/setrepository.h
    util/basicsetrepository.h
    util/includeitem.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/util COMPONENT Devel
)

install(FILES
    duchain/referencecounting.h
    duchain/parsingenvironment.h
    duchain/duchain.h
    duchain/codemodel.h
    duchain/ducontext.h
    duchain/ducontextdata.h
    duchain/topducontext.h
    duchain/topducontextutils.h
    duchain/topducontextdata.h
    duchain/declaration.h
    duchain/declarationdata.h
    duchain/classmemberdeclaration.h
    duchain/classmemberdeclarationdata.h
    duchain/classfunctiondeclaration.h
    duchain/classdeclaration.h
    duchain/functiondefinition.h
    duchain/use.h
    duchain/smartconverter.h
    duchain/forwarddeclaration.h
    duchain/duchainbase.h
    duchain/duchainpointer.h
    duchain/duchainobserver.h
    duchain/duchainlock.h
    duchain/identifier.h
    duchain/abstractfunctiondeclaration.h
    duchain/functiondeclaration.h
    duchain/stringhelpers.h
    duchain/safetycounter.h
    duchain/namespacealiasdeclaration.h
    duchain/aliasdeclaration.h
    duchain/dumpdotgraph.h
    duchain/duchainutils.h
    duchain/dumpchain.h
    duchain/indexedstring.h
    duchain/declarationid.h
    duchain/appendedlist.h
    duchain/duchainregister.h
    duchain/persistentsymboltable.h
    duchain/instantiationinformation.h
    duchain/specializationstore.h
    duchain/persistentsetmap.h
    duchain/indexeditems.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/duchain COMPONENT Devel
)

install(FILES
    duchain/types/unsuretype.h
    duchain/types/identifiedtype.h
    duchain/types/typesystem.h
    duchain/types/typeregister.h
    duchain/types/typepointer.h
    duchain/types/typesystemdata.h
    duchain/types/abstracttype.h
    duchain/types/integraltype.h
    duchain/types/functiontype.h
    duchain/types/structuretype.h
    duchain/types/pointertype.h
    duchain/types/referencetype.h
    duchain/types/delayedtype.h
    duchain/types/arraytype.h
    duchain/types/indexedtype.h
    duchain/types/enumerationtype.h
    duchain/types/constantintegraltype.h
    duchain/types/enumeratortype.h
    duchain/types/alltypes.h
    duchain/types/typeutils.h
    duchain/types/typealiastype.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/duchain/types COMPONENT Devel
)

install(FILES
    duchain/builders/abstractcontextbuilder.h
    duchain/builders/abstractdeclarationbuilder.h
    duchain/builders/abstracttypebuilder.h
    duchain/builders/abstractusebuilder.h
    duchain/builders/abstractexpressionvisitor.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/duchain/builders COMPONENT Devel
)

install(FILES
    duchain/repositories/itemrepository.h
    duchain/repositories/typerepository.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/duchain/repositories COMPONENT Devel
)

install(FILES
    codecompletion/codecompletion.h
    codecompletion/codecompletionworker.h
    codecompletion/codecompletionmodel.h
    codecompletion/codecompletionitem.h
    codecompletion/codecompletioncontext.h
    codecompletion/codecompletionitemgrouper.h
    codecompletion/codecompletionhelper.h
    codecompletion/normaldeclarationcompletionitem.h
    codecompletion/abstractincludefilecompletionitem.h
    codecompletion/codecompletiontesthelper.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/codecompletion COMPONENT Devel
)

install(FILES
    codegen/applychangeswidget.h
    codegen/createclass.h
    codegen/overridespage.h
    codegen/astchangeset.h
    codegen/duchainchangeset.h
    #codegen/codegenerator.h
    codegen/documentchangeset.h
    codegen/coderepresentation.h
    codegen/utilities.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/codegen COMPONENT Devel
)

install(FILES
    duchain/navigation/usesnavigationcontext.h
    duchain/navigation/abstractnavigationcontext.h
    duchain/navigation/abstractdeclarationnavigationcontext.h
    duchain/navigation/abstractincludenavigationcontext.h
    duchain/navigation/abstractnavigationwidget.h
    duchain/navigation/navigationaction.h
    duchain/navigation/useswidget.h
    duchain/navigation/usescollector.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/duchain/navigation COMPONENT Devel
)

install(FILES
    highlighting/codehighlighting.h
    highlighting/colorcache.h
    highlighting/configurablecolors.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/language/highlighting COMPONENT Devel
)

install(FILES
    codegen/licenses/GPL\ v2
    codegen/licenses/GPL\ v3
    codegen/licenses/LGPL\ v2
    codegen/licenses/Apache\ v2
    codegen/licenses/BSD
    codegen/licenses/Boost
    codegen/licenses/MIT\ X11
    codegen/licenses/Mozilla\ v1.1
    DESTINATION ${DATA_INSTALL_DIR}/kdevcodegen/licenses
)

